#include "ChaosImpactAssetTools.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"

bool UChaosImpactAssetTools::AllowSkeletalMeshCPUAccess(USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return false;
	}
#if WITH_EDITOR
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
