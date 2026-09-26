#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/DK/FirstGA_DKLightAttack.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifyQueue.h"
#include "Character/DKCharacter.h"
#include "Character/EnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Targeting/DKTargetLockComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Items/Weapons/DKWeapon.h"
#include "MyGameplayTags.h"
#include "MotionWarpingComponent.h"
#include "Notifies/AnimNotifyState_ComboWindow.h"
#include "UObject/StrongObjectPtr.h"

// Only seed the lock before an attack: no viewport is needed to exercise the
// real lock-change delegate, GAS ability, combo notifies and warp target cleanup.
struct FFirstTargetLockAttackTestAccess
{
	static void Lock(UDKTargetLockComponent* Component, AEnemyCharacter* Target)
	{
		Component->SetCurrentTarget(Target);
	}
};

namespace FirstComboInputTests
{

// Use the equipped sword's actual montage order and the player's actual mesh.
// Montage notifies still run on a native AnimInstance. Explicit notify delivery
// supplements real playback to reproduce duplicates and delayed old events.
// Only the delayed-close test uses a transient first-montage copy with its
// automatic ComboWindow removed, so that close cannot arrive ahead of the test.
class FFixture
{
public:
	explicit FFixture(FAutomationTestBase& InTest, bool bDeferFirstComboNotify = false, bool bStartLocked = false)
		: Test(InTest), InitialFrameCounter(GFrameCounter)
	{
		UClass* PlayerClass = LoadClass<ADKCharacter>(nullptr,
			TEXT("/Game/DKCharacter/BP_DKCharacter.BP_DKCharacter_C"));
		UClass* SwordClass = LoadClass<ADKWeapon>(nullptr,
			TEXT("/Game/DKCharacter/Weapons/BP_DKWeapon_Sword.BP_DKWeapon_Sword_C"));
		const ADKCharacter* PlayerDefaults = PlayerClass ? PlayerClass->GetDefaultObject<ADKCharacter>() : nullptr;
		const ADKWeapon* SwordDefaults = SwordClass ? SwordClass->GetDefaultObject<ADKWeapon>() : nullptr;
		if (!Test.TestNotNull(TEXT("The real player defaults load"), PlayerDefaults) ||
			!Test.TestNotNull(TEXT("The real sword defaults load"), SwordDefaults) || !GEngine)
		{
			return;
		}
		if (!Test.TestNotNull(TEXT("The real player mesh is configured"), PlayerDefaults->GetMesh()->GetSkeletalMeshAsset()) ||
			!Test.TestEqual(TEXT("The equipped sword has four light attacks"), SwordDefaults->DKWeaponData.LightAttackMontages.Num(), 4))
		{
			return;
		}
		Montages = SwordDefaults->DKWeaponData.LightAttackMontages;
		if (bDeferFirstComboNotify)
		{
			DeferredMontage.Reset(DuplicateObject<UAnimMontage>(Montages[0].Get(), GetTransientPackage(),
				MakeUniqueObjectName(GetTransientPackage(), UAnimMontage::StaticClass(), TEXT("ComboDelayedFirstAttack"))));
			if (!Test.TestNotNull(TEXT("The delayed-notify test creates an unsaved montage copy"), DeferredMontage.Get()))
			{
				return;
			}
			Montages[0] = DeferredMontage.Get();
		}
		for (int32 Index = 0; Index < Montages.Num(); ++Index)
		{
			UAnimMontage* Montage = Montages[Index];
			if (!Test.TestNotNull(TEXT("Every configured attack montage loads"), Montage)) { return; }
			const FAnimNotifyEvent* ComboNotify = nullptr;
			for (const FAnimNotifyEvent& Notify : Montage->Notifies)
			{
				if (Cast<UAnimNotifyState_ComboWindow>(Notify.NotifyStateClass))
				{
					ComboNotify = &Notify;
					break;
				}
			}
			if (Index < Montages.Num() - 1 &&
				!Test.TestNotNull(TEXT("Each non-final attack contains its real combo window"), ComboNotify))
			{
				return;
			}
			if (bDeferFirstComboNotify && Index == 0)
			{
				// Save the actual duplicated notify before removing its automatic
				// schedule. Strong references keep both sources alive during world ticks.
				DeferredComboEvent = *ComboNotify;
				DeferredComboSource.Reset(CastChecked<UAnimNotifyState_ComboWindow>(ComboNotify->NotifyStateClass));
				Montage->Notifies.RemoveAll([](const FAnimNotifyEvent& Event)
				{
					return Cast<UAnimNotifyState_ComboWindow>(Event.NotifyStateClass) != nullptr;
				});
				ComboNotify = &DeferredComboEvent;
			}
			ComboNotifies.Add(ComboNotify);
		}

		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
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
		Box->SetWorldLocation(FVector(0.f, 0.f, -50.f));
		Box->RegisterComponent();

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character = World->SpawnActor<ADKCharacter>(ADKCharacter::StaticClass(),
			FTransform(FVector(0.f, 0.f, 98.f)), SpawnParameters);
		if (!Test.TestNotNull(TEXT("The native player spawns"), Character)) { return; }
		USkeletalMeshComponent* Mesh = Character->GetMesh();
		Mesh->SetSkeletalMeshAsset(PlayerDefaults->GetMesh()->GetSkeletalMeshAsset());
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		Anim = Mesh->GetAnimInstance();
		if (!Test.TestNotNull(TEXT("The native animation instance initializes"), Anim)) { return; }
		Character->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

		ASC = Character->GetFirstAbilitySystemComponent();
		ASC->AddAttributeSetSubobject(Character->GetFirstAttributeSet());
		ASC->InitAbilityActorInfo(Character, Character);
		ASC->SetNumericAttributeBase(UFirstAttributeSet::GetMaxStaminaAttribute(), 100.f);
		ASC->SetNumericAttributeBase(UFirstAttributeSet::GetStaminaAttribute(), 100.f);
		ADKWeapon* Weapon = World->SpawnActor<ADKWeapon>(ADKWeapon::StaticClass(), FTransform::Identity, SpawnParameters);
		if (!Test.TestNotNull(TEXT("The native sword spawns"), Weapon)) { return; }
		Weapon->SetOwner(Character);
		Weapon->SetInstigator(Character);
		Weapon->DKWeaponData = SwordDefaults->DKWeaponData;
		Weapon->DKWeaponData.LightAttackMontages = Montages;
		Character->GetDKCombatComponent()->RegisterSpawnedWeapon(MyGameplayTags::DK_Weapon_Sword, Weapon, true);
		FGameplayAbilitySpec AttackSpec(UFirstGA_DKLightAttack::StaticClass(), 1);
		AttackSpec.GetDynamicSpecSourceTags().AddTag(MyGameplayTags::InputTag_LightAttack_Sword);
		AttackHandle = ASC->GiveAbility(AttackSpec);
		TickFor(0.05f);
		if (bStartLocked && !SpawnLockTarget(FVector(240.f, -60.f, 98.f))) { return; }
		Press();
		bReady = Test.TestTrue(TEXT("Light attack activates through real ASC input"), IsActive()) &&
			Test.TestEqual(TEXT("Input starts the equipped sword's first montage"), Step(), 1);
	}

	~FFixture()
	{
		if (ASC) { ASC->CancelAllAbilities(); }
		if (World)
		{
			World->EndPlay(EEndPlayReason::Quit);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
		GFrameCounter = InitialFrameCounter;
	}

	bool IsReady() const { return bReady; }
	UDKTargetLockComponent* TargetLock() const { return Character->GetTargetLockComponent(); }
	bool AttackObservesLock() const
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(AttackHandle);
		return Spec && TargetLock()->OnTargetLockChanged.Contains(Spec->GetPrimaryInstance(), FName(TEXT("HandleTargetLockChanged")));
	}
	const FMotionWarpingTarget* AttackWarpTarget() const
	{
		return Character->GetMotionWarpingComponent()->FindWarpTarget(TEXT("AttackTarget"));
	}
	AEnemyCharacter* SpawnLockTarget(const FVector& Position)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyCharacter* Enemy = World->SpawnActor<AEnemyCharacter>(AEnemyCharacter::StaticClass(),
			FTransform(Position), SpawnParameters);
		if (!Test.TestNotNull(TEXT("The lock target spawns"), Enemy)) { return nullptr; }
		Enemy->GetCharacterMovement()->DisableMovement();
		FFirstTargetLockAttackTestAccess::Lock(TargetLock(), Enemy);
		return Enemy;
	}
	bool IsActive() const
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(AttackHandle);
		return Spec && Spec->IsActive();
	}
	int32 Step() const
	{
		const UAnimMontage* Active = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		for (int32 Index = 0; Active && Index < Montages.Num(); ++Index)
		{
			if (Montages[Index] == Active) { return Index + 1; }
		}
		return 0;
	}
	void Press()
	{
		ASC->OnAbilityInputPressed(MyGameplayTags::InputTag_LightAttack_Sword);
		ObserveStep();
	}
	void Release() { ASC->OnAbilityInputReleased(MyGameplayTags::InputTag_LightAttack_Sword); }
	void Tap() { Release(); Press(); Release(); }
	void Cancel() { ASC->CancelAbilityHandle(AttackHandle); }
	bool HasAttackingTag() const { return ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Attacking); }
	bool HasStarted(int32 InStep) const { return StartedSteps.Contains(InStep); }
	int32 StartedCount() const { return StartedSequence.Num(); }
	void Open(int32 InStep)
	{
		const FAnimNotifyEvent* Event = ComboNotifies[InStep - 1];
		if (!Event) { return; }
		UAnimMontage* Montage = Montages[InStep - 1];
		CastChecked<UAnimNotifyState_ComboWindow>(Event->NotifyStateClass)->NotifyBegin(
			Character->GetMesh(), Montage, Event->GetDuration(), FAnimNotifyEventReference(Event, Montage));
		ObserveStep();
	}
	void Close(int32 InStep)
	{
		const FAnimNotifyEvent* Event = ComboNotifies[InStep - 1];
		if (!Event) { return; }
		UAnimMontage* Montage = Montages[InStep - 1];
		CastChecked<UAnimNotifyState_ComboWindow>(Event->NotifyStateClass)->NotifyEnd(
			Character->GetMesh(), Montage, FAnimNotifyEventReference(Event, Montage));
		ObserveStep();
	}
	void AdvanceToOpen(int32 InStep)
	{
		AdvanceToPosition(InStep, ComboNotifies[InStep - 1]->GetTriggerTime());
		Open(InStep);
	}
	void AdvanceToClose(int32 InStep)
	{
		AdvanceToPosition(InStep, ComboNotifies[InStep - 1]->GetEndTriggerTime());
		Close(InStep);
	}
	bool AdvanceToAutomaticBlendOut(int32 InStep)
	{
		UAnimMontage* Montage = Montages[InStep - 1];
		const int32 MaxTicks = FMath::CeilToInt(Montage->GetPlayLength() / 0.005f) + 5;
		for (int32 Tick = 0; Tick < MaxTicks && IsActive(); ++Tick)
		{
			const FAnimMontageInstance* Instance = Anim->GetInstanceForMontage(Montage);
			if (Instance && Instance->IsStopped()) { return true; }
			TickFor(0.005f);
		}
		return false;
	}
	bool IsNearNaturalBlendOut(int32 InStep) const
	{
		const UAnimMontage* Montage = Montages[InStep - 1];
		const FAnimMontageInstance* Instance = Anim->GetInstanceForMontage(Montage);
		return Instance && Instance->IsStopped() &&
			Instance->GetPosition() >= Montage->GetPlayLength() - Montage->BlendOut.GetBlendTime() - 0.02f;
	}
	void InterruptBlendingOutStep(int32 InStep)
	{
		if (FAnimMontageInstance* Instance = Anim->GetInstanceForMontage(Montages[InStep - 1]))
		{
			Instance->Stop(FAlphaBlend(0.05f), true);
		}
	}
	void FinishPlayback()
	{
		float LongestMontage = 0.f;
		for (const UAnimMontage* Montage : Montages)
		{
			LongestMontage = FMath::Max(LongestMontage, Montage->GetPlayLength());
		}
		TickFor(LongestMontage + 0.5f);
	}
	void TickFor(float Duration)
	{
		const int32 NumSteps = FMath::CeilToInt(Duration / 0.005f);
		if (NumSteps <= 0) { return; }
		const float DeltaSeconds = Duration / NumSteps;
		for (int32 Tick = 0; Tick < NumSteps; ++Tick)
		{
			World->Tick(LEVELTICK_All, DeltaSeconds);
			ObserveStep();
			++GFrameCounter;
		}
	}

private:
	void AdvanceToPosition(int32 InStep, float Position)
	{
		const FAnimMontageInstance* Instance = Anim->GetInstanceForMontage(Montages[InStep - 1]);
		if (Instance) { TickFor(FMath::Max(0.f, Position - Instance->GetPosition())); }
	}
	void ObserveStep()
	{
		const int32 Current = Step();
		if (Current > 0)
		{
			StartedSteps.Add(Current);
			if (StartedSequence.IsEmpty() || StartedSequence.Last() != Current)
			{
				StartedSequence.Add(Current);
			}
		}
	}

	FAutomationTestBase& Test;
	uint64 InitialFrameCounter;
	UWorld* World = nullptr;
	ADKCharacter* Character = nullptr;
	UFirstAbilitySystemComponent* ASC = nullptr;
	UAnimInstance* Anim = nullptr;
	TArray<TObjectPtr<UAnimMontage>> Montages;
	TArray<const FAnimNotifyEvent*> ComboNotifies;
	TStrongObjectPtr<UAnimMontage> DeferredMontage;
	TStrongObjectPtr<UAnimNotifyState_ComboWindow> DeferredComboSource;
	FAnimNotifyEvent DeferredComboEvent;
	TSet<int32> StartedSteps;
	TArray<int32> StartedSequence;
	FGameplayAbilitySpecHandle AttackHandle;
	bool bReady = false;
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstComboInputEarlyAndHoldTest,
	"First.Combat.ComboInput.EarlyBufferAndOnePressPerStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstComboInputEarlyAndHoldTest::RunTest(const FString& Parameters)
{
	{
		FirstComboInputTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) { return false; }
		Fixture.AdvanceToOpen(1);
		Fixture.AdvanceToClose(1);
		Fixture.FinishPlayback();
		TestFalse(TEXT("Holding the activation press does not queue a second slash"), Fixture.HasStarted(2));
		TestFalse(TEXT("A held single slash ends normally"), Fixture.IsActive());
	}
	{
		FirstComboInputTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) { return false; }
		for (int32 Step = 1; Step < 4; ++Step)
		{
			Fixture.TickFor(0.02f);
			Fixture.Tap();
			Fixture.AdvanceToOpen(Step);
			TestEqual(TEXT("An early fresh tap waits for this slash's close boundary"), Fixture.Step(), Step);
			Fixture.AdvanceToClose(Step);
			TestEqual(TEXT("The buffered tap starts the next real sword montage once"), Fixture.Step(), Step + 1);
		}
		Fixture.Tap();
		Fixture.Open(4);
		Fixture.Close(4);
		Fixture.FinishPlayback();
		TestEqual(TEXT("The four-slash sequence plays each step once without restarting slash one"), Fixture.StartedCount(), 4);
		TestFalse(TEXT("The final slash ends rather than wrapping the held combo"), Fixture.IsActive());
		TestFalse(TEXT("Completion releases the attacking tag"), Fixture.HasAttackingTag());
	}
	{
		FirstComboInputTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) { return false; }
		Fixture.Release();
		Fixture.TickFor(0.02f);
		Fixture.Press();
		Fixture.AdvanceToOpen(1);
		Fixture.AdvanceToClose(1);
		TestEqual(TEXT("One new held press queues the second slash"), Fixture.Step(), 2);
		Fixture.AdvanceToOpen(2);
		Fixture.AdvanceToClose(2);
		Fixture.FinishPlayback();
		TestFalse(TEXT("Holding across a transition does not also queue the third slash"), Fixture.HasStarted(3));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstComboInputGraceTest,
	"First.Combat.ComboInput.CloseBoundaryGrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstComboInputGraceTest::RunTest(const FString& Parameters)
{
	for (const float Delay : {0.05f, 0.15f})
	{
		FirstComboInputTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) { return false; }
		Fixture.Release();
		Fixture.AdvanceToOpen(1);
		Fixture.AdvanceToClose(1);
		Fixture.TickFor(Delay);
		if (!TestTrue(TEXT("The original slash is still recovering when the late tap arrives"), Fixture.IsActive()))
		{
			return false;
		}
		Fixture.Tap();
		if (Delay < 0.1f)
		{
			TestEqual(TEXT("A tap 0.05 seconds after close still reaches slash two"), Fixture.Step(), 2);
		}
		else
		{
			TestFalse(TEXT("A tap 0.15 seconds after close cannot queue slash two"), Fixture.HasStarted(2));
		}
		Fixture.FinishPlayback();
		TestEqual(TEXT("The grace decision is preserved through subsequent callbacks"), Fixture.HasStarted(2), Delay < 0.1f);
		TestFalse(TEXT("No late input leaves the ability stuck"), Fixture.IsActive());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstComboInputStaleNotifyTest,
	"First.Combat.ComboInput.PreviousMontageNotifyCannotCloseNewWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstComboInputStaleNotifyTest::RunTest(const FString& Parameters)
{
	FirstComboInputTests::FFixture Fixture(*this);
	if (!Fixture.IsReady()) { return false; }
	Fixture.Tap();
	Fixture.AdvanceToOpen(1);
	Fixture.AdvanceToClose(1);
	if (!TestEqual(TEXT("The first buffered slash reaches montage two"), Fixture.Step(), 2)) { return false; }
	Fixture.AdvanceToOpen(2);
	Fixture.Tap();
	Fixture.Open(1);
	Fixture.Close(1);
	TestEqual(TEXT("Delayed old-window events do not consume the current buffered slash"), Fixture.Step(), 2);
	Fixture.AdvanceToClose(2);
	TestEqual(TEXT("The current window retains ownership and reaches montage three"), Fixture.Step(), 3);
	Fixture.Close(1);
	Fixture.Close(2);
	Fixture.TickFor(0.03f);
	TestEqual(TEXT("Old ends after the transition cannot skip a slash"), Fixture.Step(), 3);
	Fixture.FinishPlayback();
	TestFalse(TEXT("A stale end cannot manufacture input for slash four"), Fixture.HasStarted(4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstComboInputBlendOutTest,
	"First.Combat.ComboInput.BufferedCloseDuringAutomaticBlendOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstComboInputBlendOutTest::RunTest(const FString& Parameters)
{
	for (const bool bExternalInterruption : {false, true})
	{
		FirstComboInputTests::FFixture Fixture(*this, true);
		if (!Fixture.IsReady()) { return false; }
		Fixture.Tap();
		Fixture.AdvanceToOpen(1);
		// Delay only NotifyEnd delivery to reproduce an event arriving after the
		// montage already began natural blend-out. A buffered slash must survive
		// normal completion, but never override a real interruption still pending.
		if (!TestTrue(TEXT("The real first montage reaches automatic blend-out"), Fixture.AdvanceToAutomaticBlendOut(1)))
		{
			return false;
		}
		if (!TestFalse(TEXT("The delayed close has not already started slash two"), Fixture.HasStarted(2)) ||
			!TestTrue(TEXT("The first montage reached its actual tail without an interruption"), Fixture.IsNearNaturalBlendOut(1)))
		{
			return false;
		}
		TestTrue(TEXT("The attack task remains alive during its natural blend-out"), Fixture.IsActive());
		if (bExternalInterruption) { Fixture.InterruptBlendingOutStep(1); }
		Fixture.Close(1);
		Fixture.TickFor(0.35f);
		if (bExternalInterruption)
		{
			TestFalse(TEXT("A real interruption wins over a same-frame buffered close"), Fixture.HasStarted(2));
			TestFalse(TEXT("The interrupted ability ends instead of being revived by completion"), Fixture.IsActive());
		}
		else
		{
			TestEqual(TEXT("A valid buffered close survives old blend-out and reaches the next slash"), Fixture.Step(), 2);
			TestTrue(TEXT("Old montage callbacks cannot cancel the newly started slash"), Fixture.IsActive());
		}
		Fixture.FinishPlayback();
		TestEqual(TEXT("Only normal completion consumes the buffered next slash"), Fixture.HasStarted(2), !bExternalInterruption);
		TestFalse(TEXT("The blend-out path leaves no stuck attack ability"), Fixture.IsActive());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstComboInputCancelTest,
	"First.Combat.ComboInput.CancellationClearsBufferedInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstComboInputCancelTest::RunTest(const FString& Parameters)
{
	FirstComboInputTests::FFixture Fixture(*this);
	if (!Fixture.IsReady()) { return false; }
	Fixture.Tap();
	Fixture.AdvanceToOpen(1);
	Fixture.Cancel();
	Fixture.Close(1);
	Fixture.FinishPlayback();
	TestFalse(TEXT("Cancellation cannot resurrect the buffered second slash"), Fixture.HasStarted(2));
	TestFalse(TEXT("Cancellation ends the attack ability"), Fixture.IsActive());
	TestFalse(TEXT("Cancellation returns the attacking tag"), Fixture.HasAttackingTag());
	Fixture.Tap();
	TestEqual(TEXT("A later fresh attack starts from slash one"), Fixture.Step(), 1);
	Fixture.AdvanceToOpen(1);
	Fixture.AdvanceToClose(1);
	Fixture.FinishPlayback();
	TestFalse(TEXT("Reactivation does not inherit cancelled buffered input"), Fixture.HasStarted(2));
	TestFalse(TEXT("The clean reactivation completes normally"), Fixture.IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAttackUnlockWarpTest,
	"First.Combat.TargetLock.AttackUnlockStopsWarpTracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAttackUnlockWarpTest::RunTest(const FString& Parameters)
{
	FirstComboInputTests::FFixture Fixture(*this, false, true);
	if (!Fixture.IsReady()) { return false; }
	TestNotNull(TEXT("A locked attack creates its real warp target"), Fixture.AttackWarpTarget());
	TestTrue(TEXT("The active attack subscribes to lock changes"), Fixture.AttackObservesLock());
	Fixture.TargetLock()->ClearTargetLock();
	TestNull(TEXT("Manual unlock immediately removes the old attack warp target"), Fixture.AttackWarpTarget());
	Fixture.TickFor(0.1f);
	TestTrue(TEXT("Unlock does not cancel the attack itself"), Fixture.IsActive());
	TestNull(TEXT("The old tracking timer cannot recreate the warp target"), Fixture.AttackWarpTarget());
	Fixture.Cancel();
	TestFalse(TEXT("Cancellation unsubscribes the attack from lock changes"), Fixture.AttackObservesLock());

	// An InstancedPerActor ability must bind again after a previous cancellation.
	if (!Fixture.SpawnLockTarget(FVector(240.f, 60.f, 98.f))) { return false; }
	Fixture.Tap();
	TestNotNull(TEXT("A fresh attack can acquire the new lock"), Fixture.AttackWarpTarget());
	Fixture.TargetLock()->ClearTargetLock();
	TestNull(TEXT("A reactivated attack still responds to unlock"), Fixture.AttackWarpTarget());
	Fixture.FinishPlayback();
	TestFalse(TEXT("Normal completion also unsubscribes"), Fixture.AttackObservesLock());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAttackSwitchWarpTest,
	"First.Combat.TargetLock.AttackSwitchRetargetsAtNextComboStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAttackSwitchWarpTest::RunTest(const FString& Parameters)
{
	FirstComboInputTests::FFixture Fixture(*this, false, true);
	if (!Fixture.IsReady()) { return false; }
	if (!TestNotNull(TEXT("The first slash starts with a warp target"), Fixture.AttackWarpTarget())) { return false; }
	TestTrue(TEXT("The original target is on the left"), Fixture.AttackWarpTarget()->GetLocation().Y < 0.f);
	AEnemyCharacter* NewTarget = Fixture.SpawnLockTarget(FVector(240.f, 60.f, 98.f));
	if (!NewTarget) { return false; }
	TestTrue(TEXT("Switch keeps the new lock active"), Fixture.TargetLock()->GetCurrentTarget() == NewTarget);
	TestNull(TEXT("The slash in progress does not snap to the new target"), Fixture.AttackWarpTarget());
	// Deliver the real montage's notify pair in the same frame. Screen/camera
	// acquisition is outside this test; normal input and combo transition run.
	Fixture.Open(1);
	Fixture.Tap();
	Fixture.Close(1);
	TestEqual(TEXT("The buffered input advances the real ability to slash two"), Fixture.Step(), 2);
	if (!TestNotNull(TEXT("The next slash acquires its own warp target"), Fixture.AttackWarpTarget())) { return false; }
	TestTrue(TEXT("The next slash aims at the new right-side target"), Fixture.AttackWarpTarget()->GetLocation().Y > 0.f);
	Fixture.Cancel();
	TestNull(TEXT("Cancelling the second slash cleans up its warp target"), Fixture.AttackWarpTarget());
	return true;
}

#endif
