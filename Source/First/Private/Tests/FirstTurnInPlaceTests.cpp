#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AIController.h"
#include "AbilitySystem/Abilities/DK/FirstGA_DKGuardParry.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Character/DKCharacter.h"
#include "Character/EnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/Locomotion/DKTurnInPlaceComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Targeting/DKTargetLockComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "InputActionValue.h"
#include "MotionWarpingComponent.h"
#include "MyGameplayTags.h"
#include "UObject/StrongObjectPtr.h"

// Configure editable references and seed viewport-dependent target selection.
// Movement uses the real character input handlers; root motion and cleanup remain real.
struct FFirstTurnInPlaceTestAccess
{
	static void Configure(UDKTurnInPlaceComponent* Turn, const TArray<TStrongObjectPtr<UAnimMontage>>& Montages)
	{
		Turn->TurnLeft90 = Montages[0].Get();
		Turn->TurnRight90 = Montages[1].Get();
		Turn->TurnLeft180 = Montages[2].Get();
		Turn->TurnRight180 = Montages[3].Get();
	}

	static void Lock(UDKTargetLockComponent* Lock, AEnemyCharacter* Target)
	{
		// The headless generic controller has no player-camera line of sight.
		// Keep real lock ticking, including target death and explicit unlock.
		Lock->OcclusionGraceTime = 10.f;
		Lock->SetCurrentTarget(Target);
	}

	static void Move(ADKCharacter* Character, const FVector& WorldInput)
	{
		const FRotator CameraYaw(0.f, Character->GetControlRotation().Yaw, 0.f);
		const FVector LocalInput = CameraYaw.UnrotateVector(WorldInput);
		Character->Input_Move(FInputActionValue(FVector2D(LocalInput.Y, LocalInput.X)));
	}

	static void Release(ADKCharacter* Character)
	{
		Character->Input_MoveCompleted(FInputActionValue(FVector2D::ZeroVector));
	}
};

namespace FirstTurnInPlaceTests
{

const FName TurnTargetName(TEXT("DKTurnInPlaceTarget"));
const FName AttackTargetName(TEXT("AttackTarget"));

class FFixture
{
public:
	explicit FFixture(FAutomationTestBase& InTest, bool bConfigureAnimations = true)
		: Test(InTest), InitialFrameCounter(GFrameCounter)
	{
		UClass* PlayerClass = LoadClass<ADKCharacter>(nullptr,
			TEXT("/Game/DKCharacter/BP_DKCharacter.BP_DKCharacter_C"));
		const ADKCharacter* Defaults = PlayerClass ? PlayerClass->GetDefaultObject<ADKCharacter>() : nullptr;
		if (!Test.TestNotNull(TEXT("Real player defaults load"), Defaults) || !GEngine ||
			!Test.TestNotNull(TEXT("Real player mesh loads"), Defaults->GetMesh()->GetSkeletalMeshAsset())) return;

		const TCHAR* SequenceNames[] = { TEXT("Turn_L_90_Anim"), TEXT("Turn_R_90_Anim"),
			TEXT("Turn_L_180_Anim"), TEXT("Turn_R_180_Anim") };
		for (const TCHAR* SequenceName : SequenceNames)
		{
			const FString Path = FString::Printf(TEXT("/Game/SwordAnimsetPro/Animations/Root-Motion/%s.%s"),
				SequenceName, SequenceName);
			UAnimSequence* Source = LoadObject<UAnimSequence>(nullptr, *Path);
			if (!Test.TestNotNull(TEXT("A real SwordAnimsetPro turn sequence loads"), Source)) return;
			Source->WaitOnExistingCompression();
			UAnimSequence* Sequence = DuplicateObject<UAnimSequence>(Source, GetTransientPackage(),
				MakeUniqueObjectName(GetTransientPackage(), UAnimSequence::StaticClass(), TEXT("TurnInPlaceTestSequence")));
			if (!Test.TestNotNull(TEXT("The test creates an unsaved sequence copy"), Sequence)) return;
			Sequences.Emplace(Sequence);
			Sequence->bEnableRootMotion = true;
			Sequence->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
			Sequence->WaitOnExistingCompression();
			if (!Test.TestTrue(TEXT("Turn animation already uses the player's skeleton"),
				Sequence->GetSkeleton() == Defaults->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton())) return;
			UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence,
				TEXT("DefaultSlot"), 0.03f, 0.04f, 1.f, 1, 0.f);
			if (!Test.TestNotNull(TEXT("A transient root-motion turn montage is created"), Montage)) return;
			Montages.Emplace(Montage);
		}

		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!Test.TestNotNull(TEXT("The isolated turn world is created"), World)) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->CreateAISystem();
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		AActor* Floor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
		Floor->AddInstanceComponent(Box);
		Floor->SetRootComponent(Box);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetBoxExtent(FVector(2000.f, 2000.f, 50.f));
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetGenerateOverlapEvents(false);
		Box->SetWorldLocation(FVector(0.f, 0.f, -50.f));
		Box->RegisterComponent();

		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character = World->SpawnActor<ADKCharacter>(ADKCharacter::StaticClass(),
			FTransform(FVector(0.f, 0.f, 98.f)), Spawn);
		Target = World->SpawnActor<AEnemyCharacter>(AEnemyCharacter::StaticClass(),
			FTransform(FVector(500.f, 0.f, 98.f)), Spawn);
		Controller = World->SpawnActor<AAIController>();
		if (!Test.TestNotNull(TEXT("The native player spawns"), Character) ||
			!Test.TestNotNull(TEXT("The native lock target spawns"), Target) ||
			!Test.TestNotNull(TEXT("The headless controller spawns"), Controller)) return;
		Controller->Possess(Character);
		Controller->bSetControlRotationFromPawnOrientation = false;
		USkeletalMeshComponent* Mesh = Character->GetMesh();
		Mesh->SetSkeletalMeshAsset(Defaults->GetMesh()->GetSkeletalMeshAsset());
		Mesh->SetRelativeTransform(Defaults->GetMesh()->GetRelativeTransform());
		Character->CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		Anim = Mesh->GetAnimInstance();
		if (!Test.TestNotNull(TEXT("A native animation instance initializes"), Anim)) return;
		Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
		Movement = Character->GetCharacterMovement();
		Movement->bRunPhysicsWithNoController = true;
		Movement->SetMovementMode(MOVE_Walking);
		Target->GetCharacterMovement()->DisableMovement();
		ASC = Character->GetFirstAbilitySystemComponent();
		ASC->AddAttributeSetSubobject(Character->GetFirstAttributeSet());
		ASC->InitAbilityActorInfo(Character, Character);
		ASC->SetNumericAttributeBase(UFirstAttributeSet::GetMaxStaminaAttribute(), 100.f);
		ASC->SetNumericAttributeBase(UFirstAttributeSet::GetStaminaAttribute(), 100.f);
		Character->GetDKCombatComponent()->CurrentEquippedWeaponTag = MyGameplayTags::DK_Weapon_Sword;
		Lock = Character->GetTargetLockComponent();
		Turn = Character->GetTurnInPlaceComponent();
		if (!Test.TestNotNull(TEXT("The player owns its turn component"), Turn) ||
			!Test.TestTrue(TEXT("The turn component received BeginPlay"), Turn->HasBegunPlay())) return;
		if (bConfigureAnimations) FFirstTurnInPlaceTestAccess::Configure(Turn, Montages);
		TickFor(0.05f);
		bReady = Test.TestTrue(TEXT("The player settles on a real floor"),
			Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor());
	}

	~FFixture()
	{
		if (Turn) Turn->InterruptTurn();
		if (ASC) ASC->CancelAllAbilities();
		if (World)
		{
			World->EndPlay(EEndPlayReason::Quit);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
		GFrameCounter = InitialFrameCounter;
	}

	bool IsReady() const { return bReady; }
	void SetTargetYaw(float Yaw)
	{
		Target->SetActorLocation(Character->GetActorLocation() + FRotator(0.f, Yaw, 0.f).Vector() * 500.f);
	}
	void Acquire(float Yaw)
	{
		SetTargetYaw(Yaw);
		FFirstTurnInPlaceTestAccess::Lock(Lock, Target);
	}
	bool Start(float Yaw)
	{
		Release();
		TickFor(0.2f);
		Hold(Yaw);
		return Test.TestTrue(TEXT("An unlocked stationary swordsman turns toward movement input"), Turn->IsTurningInPlace());
	}
	void Hold(float WorldYaw)
	{
		HeldInput = FRotator(0.f, WorldYaw, 0.f).Vector();
		bInputHeld = true;
		FFirstTurnInPlaceTestAccess::Move(Character, HeldInput);
	}
	void Release()
	{
		bInputHeld = false;
		HeldInput = FVector::ZeroVector;
		FFirstTurnInPlaceTestAccess::Release(Character);
	}
	void Finish()
	{
		for (int32 Step = 0; Step < 500 && Turn->IsTurningInPlace(); ++Step) TickFor(0.005f);
	}
	void TickFor(float Duration)
	{
		const int32 Steps = FMath::CeilToInt(Duration / 0.005f);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			if (Character && bInputHeld) FFirstTurnInPlaceTestAccess::Move(Character, HeldInput);
			const float OldYaw = Character ? Character->GetActorRotation().Yaw : 0.f;
			World->Tick(LEVELTICK_All, Duration / Steps);
			if (Character) AccumulatedYaw += FMath::FindDeltaAngleDegrees(OldYaw, Character->GetActorRotation().Yaw);
			++GFrameCounter;
		}
	}
	void CheckFacing(bool bOrient, bool bController)
	{
		Test.TestEqual(TEXT("Orient-to-movement respects remaining rotation owners"),
			static_cast<bool>(Movement->bOrientRotationToMovement), bOrient);
		Test.TestEqual(TEXT("Controller-facing respects remaining rotation owners"),
			static_cast<bool>(Movement->bUseControllerDesiredRotation), bController);
	}
	bool HasTurnTarget() const { return Character->GetMotionWarpingComponent()->FindWarpTarget(TurnTargetName) != nullptr; }
	void AddAttackTarget()
	{
		Character->GetMotionWarpingComponent()->AddOrUpdateWarpTargetFromTransform(AttackTargetName,
			FTransform(FVector(200.f, 0.f, 98.f)));
	}
	void CheckInterrupted()
	{
		Test.TestFalse(TEXT("Interruption ends the turn immediately"), Turn->IsTurningInPlace());
		Test.TestFalse(TEXT("Interruption removes only its turn warp target"), HasTurnTarget());
		Test.TestNotNull(TEXT("An unrelated attack warp target remains intact"),
			Character->GetMotionWarpingComponent()->FindWarpTarget(AttackTargetName));
	}
	UAnimMontage* Montage(int32 Index) const { return Montages[Index].Get(); }

	ADKCharacter* Character = nullptr;
	AAIController* Controller = nullptr;
	AEnemyCharacter* Target = nullptr;
	UAnimInstance* Anim = nullptr;
	UCharacterMovementComponent* Movement = nullptr;
	UFirstAbilitySystemComponent* ASC = nullptr;
	UDKTargetLockComponent* Lock = nullptr;
	UDKTurnInPlaceComponent* Turn = nullptr;
	float AccumulatedYaw = 0.f;

private:
	FAutomationTestBase& Test;
	uint64 InitialFrameCounter;
	UWorld* World = nullptr;
	TArray<TStrongObjectPtr<UAnimSequence>> Sequences;
	TArray<TStrongObjectPtr<UAnimMontage>> Montages;
	bool bReady = false;
	bool bInputHeld = false;
	FVector HeldInput = FVector::ZeroVector;
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTurnInPlaceRootMotionTest,
	"First.Combat.TurnInPlace.RealRootMotionMatchesCommittedYaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTurnInPlaceRootMotionTest::RunTest(const FString& Parameters)
{
	for (float TargetYaw : { -90.f, 90.f, -179.f, 179.f, -110.f, 110.f, -150.f, 150.f })
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.Start(TargetYaw)) return false;
		const int32 MontageIndex = (FMath::Abs(TargetYaw) >= 135.f ? 2 : 0) + (TargetYaw > 0.f ? 1 : 0);
		TestTrue(TEXT("Signed angle selects the expected left/right 90/180 animation"),
			Fixture.Anim->GetCurrentActiveMontage() == Fixture.Montage(MontageIndex));
		const FVector StartLocation = Fixture.Character->GetActorLocation();
		Fixture.CheckFacing(false, false);
		Fixture.Finish();
		TestFalse(TEXT("The real root-motion montage completes"), Fixture.Turn->IsTurningInPlace());
		TestTrue(FString::Printf(TEXT("Root rotation reaches committed %.0f degrees"), TargetYaw),
			FMath::Abs(FMath::FindDeltaAngleDegrees(Fixture.Character->GetActorRotation().Yaw, TargetYaw)) < 3.f);
		TestTrue(TEXT("The physical actor turns in the selected direction, including 180-degree clips"),
			Fixture.AccumulatedYaw * TargetYaw > 0.f);
		TestTrue(TEXT("Turning does not displace the character horizontally"),
			FVector::Dist2D(StartLocation, Fixture.Character->GetActorLocation()) < 2.f);
		TestFalse(TEXT("Normal completion removes the turn warp target"), Fixture.HasTurnTarget());
		Fixture.CheckFacing(true, false);
		Fixture.Release();
		Fixture.TickFor(0.3f);
		TestFalse(TEXT("A correctly aligned actor does not immediately turn again"), Fixture.Turn->IsTurningInPlace());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTurnInPlaceEligibilityTest,
	"First.Combat.TurnInPlace.ThresholdAndMissingAssetsFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTurnInPlaceEligibilityTest::RunTest(const FString& Parameters)
{
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this, false);
		if (!Fixture.IsReady()) return false;
		Fixture.TickFor(0.2f);
		const FVector StartLocation = Fixture.Character->GetActorLocation();
		Fixture.Hold(110.f);
		Fixture.TickFor(0.1f);
		TestFalse(TEXT("Missing turn montages do not start a partial turn"), Fixture.Turn->IsTurningInPlace());
		TestFalse(TEXT("Missing assets leave no turn warp target"), Fixture.HasTurnTarget());
		TestTrue(TEXT("Missing animations fall back to normal movement"),
			FVector::Dist2D(StartLocation, Fixture.Character->GetActorLocation()) > 1.f);
		Fixture.CheckFacing(true, false);
	}
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) return false;
		Fixture.TickFor(0.2f);
		const FVector StartLocation = Fixture.Character->GetActorLocation();
		Fixture.Hold(59.f);
		Fixture.TickFor(0.1f);
		TestFalse(TEXT("An angle below 60 degrees does not trigger stepping"), Fixture.Turn->IsTurningInPlace());
		TestTrue(TEXT("A small input angle starts walking immediately"),
			FVector::Dist2D(StartLocation, Fixture.Character->GetActorLocation()) > 1.f);
		Fixture.CheckFacing(true, false);
	}
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.Start(61.f)) return false;
		TestTrue(TEXT("Crossing the threshold uses a ninety-degree animation"),
			Fixture.Anim->GetCurrentActiveMontage() == Fixture.Montage(1));
	}
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) return false;
		Fixture.Character->GetDKCombatComponent()->CurrentEquippedWeaponTag = FGameplayTag();
		Fixture.TickFor(0.2f);
		const FVector StartLocation = Fixture.Character->GetActorLocation();
		Fixture.Hold(110.f);
		Fixture.TickFor(0.1f);
		TestFalse(TEXT("An unequipped character does not use sword turns"), Fixture.Turn->IsTurningInPlace());
		TestTrue(TEXT("An unequipped character can still walk"),
			FVector::Dist2D(StartLocation, Fixture.Character->GetActorLocation()) > 1.f);
		Fixture.CheckFacing(true, false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTurnInPlaceCameraIndependenceTest,
	"First.Combat.TurnInPlace.CameraAndTargetLockDoNotTriggerInputTurns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTurnInPlaceCameraIndependenceTest::RunTest(const FString& Parameters)
{
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) return false;
		const FVector StartLocation = Fixture.Character->GetActorLocation();
		const float StartYaw = Fixture.Character->GetActorRotation().Yaw;
		Fixture.Controller->SetControlRotation(FRotator(0.f, 120.f, 0.f));
		Fixture.TickFor(0.3f);
		TestFalse(TEXT("Rotating the free camera while idle never starts a turn"), Fixture.Turn->IsTurningInPlace());
		TestTrue(TEXT("An unlocked idle body does not follow camera yaw"),
			FMath::Abs(FMath::FindDeltaAngleDegrees(StartYaw, Fixture.Character->GetActorRotation().Yaw)) < 0.1f);
		TestTrue(TEXT("Idle camera input does not translate the character"),
			FVector::Dist2D(StartLocation, Fixture.Character->GetActorLocation()) < 0.1f);
		TestTrue(TEXT("The camera retains its independent orientation"),
			FMath::Abs(FMath::FindDeltaAngleDegrees(120.f, Fixture.Controller->GetControlRotation().Yaw)) < 0.1f);
		// Pressing A with that same camera produces a world direction of +30 degrees.
		Fixture.Hold(30.f);
		Fixture.TickFor(0.1f);
		TestFalse(TEXT("Input is judged against its world direction rather than camera yaw"), Fixture.Turn->IsTurningInPlace());
		TestTrue(TEXT("Camera-relative lateral input still moves normally for a small body angle"),
			FVector::Dist2D(StartLocation, Fixture.Character->GetActorLocation()) > 1.f);
	}
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) return false;
		Fixture.Acquire(110.f);
		Fixture.TickFor(0.2f);
		const FVector StartLocation = Fixture.Character->GetActorLocation();
		Fixture.Hold(-90.f);
		Fixture.TickFor(0.1f);
		TestTrue(TEXT("The ordinary target lock remains active"), Fixture.Lock->IsTargetLocked());
		TestFalse(TEXT("Locked movement does not start an input turn"), Fixture.Turn->IsTurningInPlace());
		TestTrue(TEXT("Locked lateral movement is not held behind a turn animation"),
			FVector::Dist2D(StartLocation, Fixture.Character->GetActorLocation()) > 1.f);
		Fixture.CheckFacing(false, true);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTurnInPlaceHeldInputTest,
	"First.Combat.TurnInPlace.HeldMovementWaitsForTurnThenUsesLatestDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTurnInPlaceHeldInputTest::RunTest(const FString& Parameters)
{
	for (const bool bChangeDirection : {false, true})
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.Start(-90.f)) return false;
		const FVector StartLocation = Fixture.Character->GetActorLocation();
		UAnimMontage* Montage = Fixture.Anim->GetCurrentActiveMontage();
		FAnimMontageInstance* Instance = Fixture.Anim->GetActiveInstanceForMontage(Montage);
		if (!TestNotNull(TEXT("Held-input turn owns a real montage instance"), Instance)) return false;
		const int32 InstanceID = Instance->GetInstanceID();
		Fixture.TickFor(0.1f);
		if (bChangeDirection) Fixture.Hold(0.f);
		Fixture.TickFor(0.1f);
		TestTrue(TEXT("Holding movement leaves the original turn running"), Fixture.Turn->IsTurningInPlace());
		Instance = Fixture.Anim->GetActiveInstanceForMontage(Montage);
		if (!TestNotNull(TEXT("Continuous input preserves the montage playback"), Instance)) return false;
		TestEqual(TEXT("Changing or repeating input never restarts the committed turn"), Instance->GetInstanceID(), InstanceID);
		TestTrue(TEXT("Movement input is held while the body is stepping"),
			FVector::Dist2D(StartLocation, Fixture.Character->GetActorLocation()) < 2.f);
		Fixture.Finish();
		TestFalse(TEXT("The committed turn finishes despite held input"), Fixture.Turn->IsTurningInPlace());
		TestTrue(TEXT("The committed left turn reaches its original heading"),
			FMath::Abs(FMath::FindDeltaAngleDegrees(Fixture.Character->GetActorRotation().Yaw, -90.f)) < 3.f);
		Fixture.CheckFacing(true, false);
		const FVector WalkStart = Fixture.Character->GetActorLocation();
		Fixture.TickFor(0.12f);
		const FVector WalkDelta = Fixture.Character->GetActorLocation() - WalkStart;
		const FVector ExpectedDirection = bChangeDirection ? FVector::ForwardVector : -FVector::RightVector;
		TestTrue(TEXT("The still-held key starts walking after completion"), WalkDelta.Size2D() > 1.f);
		TestTrue(TEXT("Walking uses the latest held direction"),
			FVector::DotProduct(WalkDelta.GetSafeNormal2D(), ExpectedDirection) > 0.95f);
		TestFalse(TEXT("Walking after the first turn does not queue another standstill turn"), Fixture.Turn->IsTurningInPlace());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTurnInPlaceInterruptionTest,
	"First.Combat.TurnInPlace.ActionsReleaseAndLockInterruptCleanly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTurnInPlaceInterruptionTest::RunTest(const FString& Parameters)
{
	const FGameplayTag ActionTags[] = { MyGameplayTags::DK_Status_Attacking,
		MyGameplayTags::DK_Status_Dodging, MyGameplayTags::DK_Status_HitReact,
		MyGameplayTags::DK_Status_GuardBroken, MyGameplayTags::DK_Status_Executing,
		MyGameplayTags::DK_Status_ChangingWeapon, MyGameplayTags::Shared_Status_Dead };
	for (const FGameplayTag& Tag : ActionTags)
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.Start(110.f)) return false;
		Fixture.AddAttackTarget();
		Fixture.ASC->AddLooseGameplayTag(Tag);
		Fixture.CheckInterrupted();
		Fixture.Release();
		const float InterruptedYaw = Fixture.Character->GetActorRotation().Yaw;
		// Isolate residual root rotation from the controller-facing mode restored for combat.
		Fixture.Character->SetAutomaticRotationSuppressed(Fixture.Character, true);
		Fixture.TickFor(0.15f);
		TestTrue(TEXT("The fading-out turn no longer contributes root rotation"),
			FMath::Abs(FMath::FindDeltaAngleDegrees(InterruptedYaw, Fixture.Character->GetActorRotation().Yaw)) < 0.1f);
		TestTrue(TEXT("Turn cleanup leaves the new action's tag untouched"), Fixture.ASC->HasMatchingGameplayTag(Tag));
		Fixture.Character->SetAutomaticRotationSuppressed(Fixture.Character, false);
	}
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.Start(110.f)) return false;
		Fixture.AddAttackTarget();
		Fixture.TickFor(0.1f);
		const FVector OldLocation = Fixture.Character->GetActorLocation();
		const float OldYaw = Fixture.Character->GetActorRotation().Yaw;
		Fixture.Release();
		Fixture.CheckInterrupted();
		Fixture.CheckFacing(true, false);
		Fixture.TickFor(0.2f);
		TestTrue(TEXT("Releasing the key never replays cached movement after the turn"),
			FVector::Dist2D(OldLocation, Fixture.Character->GetActorLocation()) < 0.1f);
		TestTrue(TEXT("Releasing the key removes residual root rotation during blend-out"),
			FMath::Abs(FMath::FindDeltaAngleDegrees(OldYaw, Fixture.Character->GetActorRotation().Yaw)) < 0.1f);
	}
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.Start(110.f)) return false;
		Fixture.AddAttackTarget();
		Fixture.Acquire(110.f);
		Fixture.CheckInterrupted();
		Fixture.CheckFacing(false, true);
		Fixture.Lock->ClearTargetLock();
		Fixture.CheckFacing(true, false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTurnInPlaceGuardOwnershipTest,
	"First.Combat.TurnInPlace.GuardAndLockRetainIndependentRotationOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTurnInPlaceGuardOwnershipTest::RunTest(const FString& Parameters)
{
	for (bool bGuardEndsFirst : { false, true })
	{
		FirstTurnInPlaceTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.Start(110.f)) return false;
		FGameplayAbilitySpec GuardSpec(UFirstGA_DKGuardParry::StaticClass(), 1);
		GuardSpec.GetDynamicSpecSourceTags().AddTag(MyGameplayTags::InputTag_GuardParry);
		const FGameplayAbilitySpecHandle GuardHandle = Fixture.ASC->GiveAbility(GuardSpec);
		Fixture.ASC->OnAbilityInputPressed(MyGameplayTags::InputTag_GuardParry);
		if (!TestTrue(TEXT("Real Guard activates during a low-priority turn"),
			Fixture.ASC->FindAbilitySpecFromHandle(GuardHandle)->IsActive())) return false;
		TestFalse(TEXT("Guard preempts the turn without waiting for its montage"), Fixture.Turn->IsTurningInPlace());
		TestFalse(TEXT("Guard preemption clears the turn warp target"), Fixture.HasTurnTarget());
		Fixture.CheckFacing(false, true);
		Fixture.Acquire(110.f);
		if (bGuardEndsFirst)
		{
			Fixture.ASC->CancelAbilityHandle(GuardHandle);
			Fixture.CheckFacing(false, true);
			Fixture.Lock->ClearTargetLock();
		}
		else
		{
			Fixture.Lock->ClearTargetLock();
			Fixture.CheckFacing(false, true);
			Fixture.ASC->CancelAbilityHandle(GuardHandle);
		}
		Fixture.CheckFacing(true, false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTurnInPlaceStaleCallbacksTest,
	"First.Combat.TurnInPlace.PreviousPlaybackCannotEndReplayedMontage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTurnInPlaceStaleCallbacksTest::RunTest(const FString& Parameters)
{
	FirstTurnInPlaceTests::FFixture Fixture(*this);
	if (!Fixture.IsReady() || !Fixture.Start(110.f)) return false;
	UAnimMontage* Montage = Fixture.Anim->GetCurrentActiveMontage();
	FAnimMontageInstance* OldInstance = Fixture.Anim->GetActiveInstanceForMontage(Montage);
	if (!TestNotNull(TEXT("The old playback has an instance"), OldInstance)) return false;
	const int32 OldID = OldInstance->GetInstanceID();
	// UE's queued montage events retain delegate copies, even after an unbind.
	const FOnMontageBlendingOutStarted LateBlend = OldInstance->OnMontageBlendingOutStarted;
	const FOnMontageEnded LateEnd = OldInstance->OnMontageEnded;
	Fixture.Turn->InterruptTurn();
	if (!Fixture.Start(110.f)) return false;
	FAnimMontageInstance* NewInstance = Fixture.Anim->GetActiveInstanceForMontage(Montage);
	if (!TestNotNull(TEXT("The same montage has a new playback instance"), NewInstance)) return false;
	TestTrue(TEXT("Replaying allocates a distinct instance ID"), NewInstance->GetInstanceID() != OldID);
	LateBlend.ExecuteIfBound(Montage, true);
	TestTrue(TEXT("The old interruption cannot stop the new turn"), Fixture.Turn->IsTurningInPlace());
	LateEnd.ExecuteIfBound(Montage, false);
	TestTrue(TEXT("The old completion cannot finish the new turn"), Fixture.Turn->IsTurningInPlace());
	TestTrue(TEXT("The new turn keeps its own warp target"), Fixture.HasTurnTarget());
	Fixture.Finish();
	TestFalse(TEXT("The new playback's own completion still cleans up"), Fixture.Turn->IsTurningInPlace());
	return true;
}

#endif
