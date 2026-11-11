#include "ChaosImpactEditorTools.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"

bool UChaosImpactEditorTools::AllowSkeletalCpuAccess(USkeletalMesh* Mesh, const bool bOnlyCheck)
{
#if WITH_EDITOR
	if (!Mesh)
	{
		return false;
	}
	bool bChange = false;
	for (int32 Lod = 0; Lod < Mesh->GetLODNum(); ++Lod)
	{
		const FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(Lod);
		bChange |= Info && !Info->bAllowCPUAccess;
	}
	if (!bChange || bOnlyCheck)
	{
		return bChange;
	}
	Mesh->Modify();
	for (int32 Lod = 0; Lod < Mesh->GetLODNum(); ++Lod)
	{
		if (FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(Lod))
		{
			Info->bAllowCPUAccess = true;
		}
	}
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}
