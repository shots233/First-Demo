#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/BOSS/FirstBossPursuitAnimation.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Character/BossCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossDodgeMontageAssetTest,
	"First.Combat.BossPursuit.DodgeMontageAssetValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossDodgeMontageAssetTest::RunTest(const FString& Parameters)
{
	UClass* BossClass = LoadClass<ABossCharacter>(nullptr, TEXT("/Game/Enemy/BP_Boss.BP_Boss_C"));
	const ABossCharacter* Boss = BossClass ? BossClass->GetDefaultObject<ABossCharacter>() : nullptr;
	UAnimMontage* Asset = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Enemy/AnimBP/Montages/AM_Boss_Dodge_F.AM_Boss_Dodge_F"));
	if (!TestNotNull(TEXT("The real BOSS defaults load"), Boss) ||
		!TestNotNull(TEXT("The user's saved forward-dodge montage loads"), Asset))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("Dodge montage: length=%f rate=%f root=%d autoBlend=%d slots=%d sections=%d bossSkeleton=%s montageSkeleton=%s rootTravel=%s"),
		Asset->GetPlayLength(), Asset->RateScale, Asset->HasRootMotion(), Asset->bEnableAutoBlendOut,
		Asset->SlotAnimTracks.Num(), Asset->CompositeSections.Num(),
		*GetPathNameSafe(Boss->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton()), *GetPathNameSafe(Asset->GetSkeleton()),
		*Asset->ExtractRootMotionFromTrackRange(0.f, Asset->GetPlayLength(), FAnimExtractContext(0.0, true)).GetTranslation().ToString()));
	for (const FCompositeSection& Section : Asset->CompositeSections)
	{
		AddInfo(FString::Printf(TEXT("Section %s time=%f next=%s"), *Section.SectionName.ToString(), Section.GetTime(), *Section.NextSectionName.ToString()));
	}
	for (const FSlotAnimationTrack& Track : Asset->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
		{
			const UAnimSequence* Sequence = Cast<UAnimSequence>(Segment.GetAnimReference());
			AddInfo(FString::Printf(TEXT("Slot %s segment rate=%f loops=%d sequence=%s root=%d seqRate=%f"),
				*Track.SlotName.ToString(), Segment.AnimPlayRate, Segment.LoopingCount,
				*GetPathNameSafe(Sequence), Sequence ? Sequence->bEnableRootMotion : false, Sequence ? Sequence->RateScale : 0.f));
			if (Sequence)
			{
				AddInfo(FString::Printf(TEXT("RootBone0=%s namedRootIndex=%d sequenceMotion=%s segmentStart=%f animStart=%f animEnd=%f"),
					*Sequence->GetSkeleton()->GetReferenceSkeleton().GetBoneName(0).ToString(),
					Sequence->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(TEXT("root")),
					*Sequence->ExtractRootMotionFromRange(0.0, static_cast<double>(Sequence->GetPlayLength()), FAnimExtractContext()).GetTranslation().ToString(),
					Segment.StartPos, Segment.AnimStartTime, Segment.AnimEndTime));
				for (bool bRaw : {false, true})
				{
					FTransform Start = FTransform::Identity, End = FTransform::Identity;
					Sequence->GetBoneTransform(Start, FSkeletonPoseBoneIndex(0), FAnimExtractContext(0.0), bRaw);
					Sequence->GetBoneTransform(End, FSkeletonPoseBoneIndex(0), FAnimExtractContext(static_cast<double>(Sequence->GetPlayLength())), bRaw);
					AddInfo(FString::Printf(TEXT("Bone0 raw=%d start=%s end=%s"), bRaw, *Start.ToString(), *End.ToString()));
				}
				// 刚载入的编辑器资源可能仍在异步压缩；测试等待它完成后再验收。
				// 运行中的能力不做此阻塞等待，打包游戏也不需要编辑器压缩接口。
				const_cast<UAnimSequence*>(Sequence)->WaitOnExistingCompression();
				AddInfo(FString::Printf(TEXT("Sequence motion after compression wait=%s"),
					*Sequence->ExtractRootMotionFromRange(0.0, static_cast<double>(Sequence->GetPlayLength()), FAnimExtractContext()).GetTranslation().ToString()));
			}
		}
	}
	if (!TestTrue(TEXT("The real root-motion dodge montage can enter the pursuit"),
		FirstBossPursuit::IsDodgeMontageUsable(Boss, Asset)))
	{
		return false;
	}

	// Only transient copies are changed: malformed editor settings must not
	// leave a BOSS waiting forever for a forward-dodge completion callback.
	TStrongObjectPtr<UAnimMontage> Copy(DuplicateObject<UAnimMontage>(Asset, GetTransientPackage()));
	if (!TestNotNull(TEXT("A transient montage copy is created"), Copy.Get())) return false;
	const FName OriginalNextSection = Copy->CompositeSections[0].NextSectionName;
	Copy->CompositeSections[0].NextSectionName = Copy->CompositeSections[0].SectionName;
	TestFalse(TEXT("A section looping back to itself cannot start a pursuit dodge"),
		FirstBossPursuit::IsDodgeMontageUsable(Boss, Copy.Get()));
	Copy->CompositeSections[0].NextSectionName = OriginalNextSection;

	Copy->bEnableAutoBlendOut = false;
	TestFalse(TEXT("A montage that holds its final frame cannot start a pursuit dodge"),
		FirstBossPursuit::IsDodgeMontageUsable(Boss, Copy.Get()));
	Copy->bEnableAutoBlendOut = true;
	TestTrue(TEXT("Restoring completion settings makes the montage usable again"),
		FirstBossPursuit::IsDodgeMontageUsable(Boss, Copy.Get()));

	FAnimSegment& Segment = Copy->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
	UAnimSequence* OriginalSequence = Cast<UAnimSequence>(Segment.GetAnimReference());
	if (!TestNotNull(TEXT("The valid dodge montage contains an animation sequence"), OriginalSequence)) return false;
	TStrongObjectPtr<UAnimSequence> NoRootSequence(
		DuplicateObject<UAnimSequence>(OriginalSequence, GetTransientPackage()));
	if (!TestNotNull(TEXT("A transient sequence copy is created"), NoRootSequence.Get())) return false;
	NoRootSequence->bEnableRootMotion = false;
	Segment.SetAnimReference(NoRootSequence.Get());
	TestFalse(TEXT("An animation with root-motion extraction disabled cannot start a pursuit dodge"),
		FirstBossPursuit::IsDodgeMontageUsable(Boss, Copy.Get()));
	TestTrue(TEXT("The original saved montage remains usable and unchanged"),
		FirstBossPursuit::IsDodgeMontageUsable(Boss, Asset));
	return true;
}

#endif
