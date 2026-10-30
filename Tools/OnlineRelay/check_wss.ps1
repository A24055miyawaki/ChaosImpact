param([string]$Server = 'wss://132-145-120-172.sslip.io')
# Windows PowerShell 5.1 / PowerShell 7. Uses Windows TLS trust; no Python or external module.
$ErrorActionPreference = 'Stop'
$relayWs = [System.Net.WebSockets.ClientWebSocket]::new()
$relayCancel = [System.Threading.CancellationTokenSource]::new()
$relayCancel.CancelAfter(12000)
try {
    [void]$relayWs.ConnectAsync([uri]($Server.TrimEnd('/') + '/directory'), $relayCancel.Token).GetAwaiter().GetResult()
    $relayBody = [Text.Encoding]::UTF8.GetBytes('{"op":"search","ver":"any","password":"0000","rid":73}')
    $relaySend = [ArraySegment[byte]]::new($relayBody)
    [void]$relayWs.SendAsync($relaySend, [Net.WebSockets.WebSocketMessageType]::Text, $true, $relayCancel.Token).GetAwaiter().GetResult()
    $relayBuffer = [byte[]]::new(16384)
    $relayText = [Text.StringBuilder]::new()
    do {
        $relayResult = $relayWs.ReceiveAsync([ArraySegment[byte]]::new($relayBuffer), $relayCancel.Token).GetAwaiter().GetResult()
        if ($relayResult.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) { throw 'Server closed the connection.' }
        [void]$relayText.Append([Text.Encoding]::UTF8.GetString($relayBuffer, 0, $relayResult.Count))
        if ($relayText.Length -gt 65536) { throw 'Unexpected response size.' }
    } until ($relayResult.EndOfMessage)
    $relayReply = $relayText.ToString() | ConvertFrom-Json
    if ($relayReply.rid -ne 73 -or !$relayReply.ok) { throw 'Unexpected relay response.' }
    Write-Output 'SUCCESS: WSS/TCP443 and certificate verification passed.'
} catch {
    Write-Output ('FAILED: ' + $_.Exception.GetBaseException().Message)
    exit 1
} finally {
    $relayWs.Dispose()
    $relayCancel.Dispose()
}
