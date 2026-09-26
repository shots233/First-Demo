#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/DK/FirstGA_DKGuardParry.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Character/DKCharacter.h"
#include "Character/EnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/Targeting/DKTargetLockComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "MyGameplayTags.h"

// A headless world cannot select through a viewport. Set only the initial target;
// teardown, target destruction and Guard activation use their production paths.
struct FFirstTargetLockTestAccess
{
	static void Lock(UDKTargetLockComponent* Component, AEnemyCharacter* Target)
	{
		Component->SetCurrentTarget(Target);
	}

	static float NextSwitchTime(const UDKTargetLockComponent* Component)
	{
		return Component->NextAllowedSwitchTime;
	}
};

namespace FirstTargetLockTests
{

class FFixture
{
public:
	FFixture(FAutomationTestBase& InTest)
		: Test(InTest), InitialFrameCounter(GFrameCounter)
	{
		if (!Test.TestNotNull(TEXT("The engine is available"), GEngine))
		{
			return;
		}
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!Test.TestNotNull(TEXT("The target-lock test world was created"), World))
		{
			return;
		}
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		if (!Test.TestTrue(TEXT("The test world entered play"), World->GetBegunPlay()))
		{
			return;
		}

		AActor* Floor = World->SpawnActor<AActor>();
		if (!Test.TestNotNull(TEXT("The collision floor was spawned"), Floor))
		{
			return;
		}
		UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
		Floor->AddInstanceComponent(Box);
		Floor->SetRootComponent(Box);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetBoxExtent(FVector(2000.f, 2000.f, 50.f));
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetGenerateOverlapEvents(false);
		Box->SetWorldLocation(FVector(0.f, 0.f, -50.f));
		Box->RegisterComponent();

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character = World->SpawnActor<ADKCharacter>(ADKCharacter::StaticClass(),
			FTransform(FVector(0.f, 0.f, 98.f)), SpawnParameters);
		FirstTarget = World->SpawnActor<AEnemyCharacter>(AEnemyCharacter::StaticClass(),
			FTransform(FVector(500.f, 0.f, 98.f)), SpawnParameters);
		SecondTarget = World->SpawnActor<AEnemyCharacter>(AEnemyCharacter::StaticClass(),
			FTransform(FVector(500.f, 300.f, 98.f)), SpawnParameters);
		if (!Test.TestNotNull(TEXT("The native player was spawned"), Character) ||
			!Test.TestNotNull(TEXT("The first native target was spawned"), FirstTarget) ||
			!Test.TestNotNull(TEXT("The second native target was spawned"), SecondTarget))
		{
			return;
		}

		Lock = Character->GetTargetLockComponent();
		ASC = Character->GetFirstAbilitySystemComponent();
		Movement = Character->GetCharacterMovement();
		if (!Test.TestNotNull(TEXT("The player owns its target-lock component"), Lock) ||
			!Test.TestTrue(TEXT("Target lock received BeginPlay"), Lock->HasBegunPlay()))
		{
			return;
		}
		ASC->AddAttributeSetSubobject(Character->GetFirstAttributeSet());
		ASC->InitAbilityActorInfo(Character, Character);
		ASC->SetNumericAttributeBase(UFirstAttributeSet::GetMaxStaminaAttribute(), 100.f);
		ASC->SetNumericAttributeBase(UFirstAttributeSet::GetStaminaAttribute(), 100.f);
		Character->GetDKCombatComponent()->CurrentEquippedWeaponTag = MyGameplayTags::DK_Weapon_Sword;
		Movement->bRunPhysicsWithNoController = true;
		Movement->SetMovementMode(MOVE_Walking);
		FirstTarget->GetCharacterMovement()->DisableMovement();
		SecondTarget->GetCharacterMovement()->DisableMovement();

		FGameplayAbilitySpec GuardSpec(UFirstGA_DKGuardParry::StaticClass(), 1);
		GuardSpec.GetDynamicSpecSourceTags().AddTag(MyGameplayTags::InputTag_GuardParry);
		GuardHandle = ASC->GiveAbility(GuardSpec);

		for (int32 Step = 0; Step < 10; ++Step)
		{
			World->Tick(LEVELTICK_All, 0.005f);
			++GFrameCounter;
		}
		bReady = Test.TestTrue(TEXT("The player settles on a real collision floor"),
			Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor());
	}

	~FFixture()
	{
		if (ASC)
		{
			ASC->CancelAllAbilities();
		}
		if (World)
		{
			World->EndPlay(EEndPlayReason::Quit);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
		GFrameCounter = InitialFrameCounter;
	}

	bool IsReady() const { return bReady; }
	void Acquire(AEnemyCharacter* Target) { FFirstTargetLockTestAccess::Lock(Lock, Target); }
	bool StartGuard()
	{
		ASC->OnAbilityInputPressed(MyGameplayTags::InputTag_GuardParry);
		return Test.TestTrue(TEXT("Guard activates through real ASC input"), IsGuardActive()) &&
			Test.TestTrue(TEXT("Guard owns its defense tag"),
				ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Defending));
	}
	void CancelGuard()
	{
		ASC->CancelAbilityHandle(GuardHandle);
		Test.TestFalse(TEXT("The actual Guard ability ended"), IsGuardActive());
	}
	bool IsGuardActive() const
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(GuardHandle);
		return Spec && Spec->IsActive();
	}
	void CheckFacing(bool bOrientToMovement, bool bControllerDesired)
	{
		Test.TestEqual(TEXT("Orient-to-movement matches the remaining owners"),
			static_cast<bool>(Movement->bOrientRotationToMovement), bOrientToMovement);
		Test.TestEqual(TEXT("Controller-facing matches the remaining owners"),
			static_cast<bool>(Movement->bUseControllerDesiredRotation), bControllerDesired);
	}
	void CheckLocked(AEnemyCharacter* ExpectedTarget)
	{
		Test.TestTrue(TEXT("Lock remains active"), Lock->IsTargetLocked());
		Test.TestTrue(TEXT("The expected target remains selected"), Lock->GetCurrentTarget() == ExpectedTarget);
		Test.TestTrue(TEXT("Lock tick is enabled"), Lock->IsComponentTickEnabled());
		Test.TestEqual(TEXT("Lock owns exactly one tag layer"),
			ASC->GetTagCount(MyGameplayTags::DK_Status_TargetLocked), 1);
	}
	void CheckUnlocked()
	{
		Test.TestFalse(TEXT("Lock has been cleared"), Lock->IsTargetLocked());
		Test.TestNull(TEXT("No stale current target remains"), Lock->GetCurrentTarget());
		Test.TestFalse(TEXT("Lock tick is disabled"), Lock->IsComponentTickEnabled());
		Test.TestEqual(TEXT("The lock tag was released"),
			ASC->GetTagCount(MyGameplayTags::DK_Status_TargetLocked), 0);
	}

	ADKCharacter* Character = nullptr;
	AEnemyCharacter* FirstTarget = nullptr;
	AEnemyCharacter* SecondTarget = nullptr;
	UDKTargetLockComponent* Lock = nullptr;
	UFirstAbilitySystemComponent* ASC = nullptr;
	UCharacterMovementComponent* Movement = nullptr;

private:
	FAutomationTestBase& Test;
	uint64 InitialFrameCounter;
	UWorld* World = nullptr;
	FGameplayAbilitySpecHandle GuardHandle;
	bool bReady = false;
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTargetLockDestroyTest,
	"First.Combat.TargetLock.TargetDestroyClearsImmediately",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTargetLockDestroyTest::RunTest(const FString& Parameters)
{
	FirstTargetLockTests::FFixture Fixture(*this);
	if (!Fixture.IsReady()) { return false; }
	Fixture.Acquire(Fixture.FirstTarget);
	Fixture.CheckLocked(Fixture.FirstTarget);
	Fixture.CheckFacing(false, true);
	TestTrue(TEXT("Destroy accepts the currently locked target"), Fixture.FirstTarget->Destroy());
	// No world tick: the target's EndPlay must remove the lock immediately.
	Fixture.CheckUnlocked();
	Fixture.CheckFacing(true, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTargetLockReplacementTest,
	"First.Combat.TargetLock.ReplacementUnbindsOldTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTargetLockReplacementTest::RunTest(const FString& Parameters)
{
	FirstTargetLockTests::FFixture Fixture(*this);
	if (!Fixture.IsReady()) { return false; }
	Fixture.Acquire(Fixture.FirstTarget);
	Fixture.Acquire(Fixture.SecondTarget);
	TestTrue(TEXT("Destroy accepts the replaced target"), Fixture.FirstTarget->Destroy());
	Fixture.CheckLocked(Fixture.SecondTarget);
	Fixture.CheckFacing(false, true);
	TestTrue(TEXT("Destroy accepts the new target"), Fixture.SecondTarget->Destroy());
	Fixture.CheckUnlocked();
	Fixture.CheckFacing(true, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTargetLockGuardEndOrderTest,
	"First.Combat.TargetLock.GuardAndLockCanEndInEitherOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTargetLockGuardEndOrderTest::RunTest(const FString& Parameters)
{
	for (const bool bGuardEndsFirst : {false, true})
	{
		FirstTargetLockTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) { return false; }
		Fixture.Acquire(Fixture.FirstTarget);
		if (!Fixture.StartGuard()) { return false; }
		if (bGuardEndsFirst)
		{
			Fixture.CancelGuard();
			Fixture.CheckLocked(Fixture.FirstTarget);
			Fixture.CheckFacing(false, true);
			Fixture.Lock->ClearTargetLock();
		}
		else
		{
			Fixture.Lock->ClearTargetLock();
			TestTrue(TEXT("Clearing lock leaves Guard active"), Fixture.IsGuardActive());
			Fixture.CheckFacing(false, true);
			Fixture.CancelGuard();
		}
		Fixture.CheckUnlocked();
		Fixture.CheckFacing(true, false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTargetLockCustomBaselineTest,
	"First.Combat.TargetLock.RestoresCustomMovementBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTargetLockCustomBaselineTest::RunTest(const FString& Parameters)
{
	FirstTargetLockTests::FFixture Fixture(*this);
	if (!Fixture.IsReady()) { return false; }
	Fixture.Movement->bOrientRotationToMovement = false;
	Fixture.Movement->bUseControllerDesiredRotation = false;
	Fixture.Acquire(Fixture.FirstTarget);
	if (!Fixture.StartGuard()) { return false; }
	Fixture.Lock->ClearTargetLock();
	Fixture.CheckFacing(false, true);
	Fixture.CancelGuard();
	Fixture.CheckUnlocked();
	Fixture.CheckFacing(false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTargetLockReacquireDuringGuardTest,
	"First.Combat.TargetLock.ReacquireDuringGuardPreservesOriginalBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTargetLockReacquireDuringGuardTest::RunTest(const FString& Parameters)
{
	for (const bool bGuardEndsFirst : {false, true})
	{
		FirstTargetLockTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) { return false; }
		Fixture.Acquire(Fixture.FirstTarget);
		if (!Fixture.StartGuard()) { return false; }
		// Destruction clears the initial lock, while Guard keeps controller facing.
		TestTrue(TEXT("The initial target is destroyed during Guard"), Fixture.FirstTarget->Destroy());
		Fixture.CheckUnlocked();
		Fixture.CheckFacing(false, true);
		Fixture.Acquire(Fixture.SecondTarget);
		Fixture.Acquire(Fixture.SecondTarget);
		Fixture.CheckLocked(Fixture.SecondTarget);
		if (bGuardEndsFirst)
		{
			Fixture.CancelGuard();
			Fixture.CheckLocked(Fixture.SecondTarget);
			Fixture.CheckFacing(false, true);
			Fixture.Lock->ClearTargetLock();
		}
		else
		{
			Fixture.Lock->ClearTargetLock();
			Fixture.CheckFacing(false, true);
			Fixture.CancelGuard();
		}
		Fixture.CheckUnlocked();
		Fixture.CheckFacing(true, false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstTargetLockFailedSwitchTest,
	"First.Combat.TargetLock.FailedSwitchDoesNotConsumeCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstTargetLockFailedSwitchTest::RunTest(const FString& Parameters)
{
	FirstTargetLockTests::FFixture Fixture(*this);
	if (!Fixture.IsReady()) { return false; }
	Fixture.Acquire(Fixture.FirstTarget);
	const float PreviousSwitchTime = FFirstTargetLockTestAccess::NextSwitchTime(Fixture.Lock);
	// There is deliberately no player controller or viewport. Switching must fail
	// without charging cooldown, so an immediate opposite-direction retry is free.
	Fixture.Lock->SwitchTarget(1.f);
	TestEqual(TEXT("A failed right switch leaves cooldown unchanged"),
		FFirstTargetLockTestAccess::NextSwitchTime(Fixture.Lock), PreviousSwitchTime);
	Fixture.Lock->SwitchTarget(-1.f);
	TestEqual(TEXT("A failed left retry leaves cooldown unchanged"),
		FFirstTargetLockTestAccess::NextSwitchTime(Fixture.Lock), PreviousSwitchTime);
	Fixture.CheckLocked(Fixture.FirstTarget);
	Fixture.CheckFacing(false, true);
	return true;
}

#endif
