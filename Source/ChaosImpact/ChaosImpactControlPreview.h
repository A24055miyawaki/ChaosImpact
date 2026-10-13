#pragma once
#include "ChaosImpactSettings.h"
struct FGeometry;
class FSlateWindowElementList;
struct FSlateBrush;

namespace ChaosImpactControlPreview
{
	/** Device diagram plus a recorded gameplay movie; never runs or changes the live match. */
	void Paint(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, int32 Profile,
		EChaosImpactDevice Device, EChaosImpactControlScheme Scheme, double VideoTime,
		const FSlateBrush* VideoBrush, bool bVideoFailed);
}
