#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "ChaosImpactTackleComponent.generated.h"

class AChaosImpactCharacter;

USTRUCT()
struct FChaosImpactTackleState
{
    GENERATED_BODY()
    UPROPERTY() double Started = -100;
    UPROPERTY() double ReadyAt = 0;
    UPROPERTY() double StunnedUntil = 0;
    UPROPERTY() FVector_NetQuantizeNormal Direction = FVector::ForwardVector;
};

/** A vulnerable shoulder rush, never a damage-dealing or invulnerable dash. Server decides contacts. */
UCLASS(ClassGroup=(ChaosImpact))
class UChaosImpactTackleComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UChaosImpactTackleComponent();
    UPROPERTY(EditAnywhere, Category="Chaos Impact|Tackle") float ActiveSeconds = .20f;
    UPROPERTY(EditAnywhere, Category="Chaos Impact|Tackle") float RecoverySeconds = .22f;
    UPROPERTY(EditAnywhere, Category="Chaos Impact|Tackle") float CooldownSeconds = 1.1f;
    UPROPERTY(EditAnywhere, Category="Chaos Impact|Tackle") float StaminaCost = 1.f;
    UPROPERTY(EditAnywhere, Category="Chaos Impact|Tackle") float RushSpeed = 1100.f;
    UPROPERTY(EditAnywhere, Category="Chaos Impact|Tackle", meta=(ClampMin="0")) float KnockbackSpeed = 1500.f;
    UPROPERTY(EditAnywhere, Category="Chaos Impact|Tackle", meta=(ClampMin="0")) float KnockbackUpwardSpeed = 300.f;
    bool Request();
    bool IsBusy() const;
    bool IsActive() const;
    double GetStunDeadline() const { return FMath::Max(State.StunnedUntil,ClientStunUntil); }
    bool CanStart() const;
    bool HandleKey(const FKey& Key, EInputEvent Event);
    bool HandleInput();
    void QueueSimpleDash();
    void ResetInput();
    void ResetAction();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    AChaosImpactCharacter* Player() const;
    double Now() const;
    bool ComboDown() const;
    FVector PickDirection() const;
    void CheckContact();
    void ApplyContact(AChaosImpactCharacter* Victim);
    void InterruptAndDrop();
    void Kick(AChaosImpactCharacter* Who, const FVector& Velocity);
    void UpdatePose(float DeltaTime);
    UFUNCTION(Server, Reliable) void ServerRequest(FVector_NetQuantizeNormal Direction);
    UFUNCTION(Client, Reliable) void ClientInterrupt(double StunnedUntil);
    UFUNCTION(NetMulticast, Unreliable) void MulticastImpact(FVector_NetQuantize At, FVector_NetQuantizeNormal Direction);
    UFUNCTION(NetMulticast, Unreliable) void MulticastRush(FVector_NetQuantize At, FVector_NetQuantizeNormal Direction);
    UPROPERTY(Replicated) FChaosImpactTackleState State;
    double RequestAgainAt = 0;
    double ClientStunUntil = 0;
    double PendingDashAt = -1;
    double ObservedStart = -100;
    double LastTickAt = -100;
    bool bComboLatched = false;
    bool bThrowEventDown = false;
    bool bDashEventDown = false;
    double ThrowEventAt = -100;
    double DashEventAt = -100;
    bool bHit = false;
    bool bStoppedRush = false;
    bool bPoseSaved = false;
    float StartHealth = 0;
    FTransform SavedPose;
    FVector PreviousLocation = FVector::ZeroVector;
};
