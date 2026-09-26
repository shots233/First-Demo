#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilitySystem/Abilities/DK/FirstGA_DKDodge.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "AbilitySystem/GameplayEffects/FirstGE_StaminaRegen.h"
#include "AbilitySystem/GameplayEffects/FirstGE_StaminaRegenPause.h"
#include "Character/DKCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "MyGameplayTags.h"

// Exercise the same pressed/released handlers as Enhanced Input without changing
// production input visibility or requiring a local player / project input assets.
class FFirstSprintStaminaTestAccess
{
public:
	static void Press(ADKCharacter* Character)
	{
		Character->Input_AbilityInputPressed(MyGameplayTags::InputTag_Dodge);
	}
	static void Release(ADKCharacter* Character)
	{
		Character->Input_AbilityInputReleased(MyGameplayTags::InputTag_Dodge);
	}
};

namespace FirstSprintStaminaTests
{
class FFixture
{
public:
	explicit FFixture(FAutomationTestBase& InTest) : Test(InTest), InitialFrameCounter(GFrameCounter)
	{
		if (!GEngine) { return; }
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World) { return; }
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		AddBox(FVector(0.f, 0.f, -50.f), FVector(20000.f, 20000.f, 50.f));

		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character = World->SpawnActor<ADKCharacter>(ADKCharacter::StaticClass(),
			FTransform(FVector(0.f, 0.f, 98.f)), Spawn);
		if (!Character) { return; }
		ASC = Character->GetFirstAbilitySystemComponent();
		Movement = Character->GetCharacterMovement();
		Attributes = Character->GetFirstAttributeSet();
		ASC->AddAttributeSetSubobject(Attributes);
		ASC->InitAbilityActorInfo(Character, Character);
		ASC->SetNumericAttributeBase(UFirstAttributeSet::GetMaxStaminaAttribute(), 100.f);
		SetStamina(100.f);
		Movement->bRunPhysicsWithNoController = true;
		Movement->SetMovementMode(MOVE_Walking);
		FGameplayAbilitySpec Dodge(UFirstGA_DKDodge::StaticClass(), 1);
		Dodge.GetDynamicSpecSourceTags().AddTag(MyGameplayTags::InputTag_Dodge);
		DodgeHandle = ASC->GiveAbility(Dodge);
		TickFor(0.05f);
		FFirstSprintStaminaTestAccess::Press(Character);
		TickFor(0.4f);
		bReady = Test.TestTrue(TEXT("The real input hold timer enables running after Dodge"),
			FMath::IsNearlyEqual(Movement->MaxWalkSpeed, 500.f)) &&
			Test.TestEqual(TEXT("The initial Dodge costs twenty, stationary holding adds no sprint cost"), Stamina(), 80.f) &&
			Test.TestTrue(TEXT("The character is grounded on a real collision floor"), Movement->CurrentFloor.IsWalkableFloor());
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
	float Stamina() const { return Attributes->GetStamina(); }
	void SetStamina(float Value) { ASC->SetNumericAttributeBase(UFirstAttributeSet::GetStaminaAttribute(), Value); }
	int32 PauseCount() const { return ASC->GetGameplayTagCount(MyGameplayTags::DK_Status_StaminaRegenPaused); }
	int32 ExhaustionCount() const { return ASC->GetGameplayTagCount(MyGameplayTags::DK_Status_DodgeExhausted); }
	bool TryDodge() { return ASC->TryActivateAbility(DodgeHandle, false); }
	bool IsDodging() const { return ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Dodging); }
	void AddRegen()
	{
		ASC->ApplyGameplayEffectToSelf(GetDefault<UFirstGE_StaminaRegen>(), 1.f, ASC->MakeEffectContext());
	}
	void AddGuardPause()
	{
		ASC->ApplyGameplayEffectToSelf(GetDefault<UFirstGE_StaminaRegenPause>(), 1.f, ASC->MakeEffectContext());
	}
	void TickFor(float Duration, bool bMove = false, float StepSize = 0.01f)
	{
		// Equal steps avoid an extra near-zero remainder frame being clamped up
		// to the world's minimum frame time, which would simulate extra time.
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Duration / StepSize - 0.0001f));
		const float Step = Duration / Steps;
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			if (bMove) { Character->AddMovementInput(FVector::ForwardVector, 1.f, true); }
			World->Tick(LEVELTICK_All, Step);
			++GFrameCounter;
		}
	}
	void AllowLongTestFrame()
	{
		// Only this isolated test world bypasses the usual maximum-frame clamp.
		World->GetWorldSettings()->MaxUndilatedFrameTime = 1.f;
		Movement->MaxSimulationIterations = 32;
	}
	void AddBox(const FVector& Position, const FVector& Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->AddInstanceComponent(Box);
		Actor->SetRootComponent(Box);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetWorldLocation(Position);
		Box->RegisterComponent();
	}

	ADKCharacter* Character = nullptr;
	UFirstAbilitySystemComponent* ASC = nullptr;
	UCharacterMovementComponent* Movement = nullptr;

private:
	FAutomationTestBase& Test;
	uint64 InitialFrameCounter;
	UWorld* World = nullptr;
	UFirstAttributeSet* Attributes = nullptr;
	FGameplayAbilitySpecHandle DodgeHandle;
	bool bReady = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstSprintRateTest, "First.Combat.SprintStamina.RateAndMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFirstSprintRateTest::RunTest(const FString& Parameters)
{
	for (float Step : { 0.1f, 1.f / 30.f, 1.f / 120.f })
	{
		FirstSprintStaminaTests::FFixture F(*this);
		if (!F.IsReady()) { return false; }
		F.AddRegen();
		F.SetStamina(50.f);
		F.TickFor(1.f, true, Step);
		TestTrue(TEXT("One second of actual running costs five, with normal regeneration active"),
			FMath::IsNearlyEqual(F.Stamina(), 45.f, 0.005f));
		TestEqual(TEXT("Running owns exactly one regeneration pause"), F.PauseCount(), 1);
		F.TickFor(0.2f);
		TestTrue(TEXT("Holding Shift without movement does not charge sprint stamina"),
			FMath::IsNearlyEqual(F.Stamina(), 45.f, 0.005f));
	}

	FirstSprintStaminaTests::FFixture F(*this);
	if (!F.IsReady()) { return false; }
	F.SetStamina(50.f);
	F.AddBox(F.Character->GetActorLocation() + FVector(80.f, 0.f, 50.f), FVector(10.f, 500.f, 200.f));
	F.TickFor(0.5f, true);
	const float AtWall = F.Stamina();
	F.TickFor(0.5f, true);
	TestEqual(TEXT("Continuing to push against an impassable wall does not charge"), F.Stamina(), AtWall);

	FirstSprintStaminaTests::FFixture Actions(*this);
	if (!Actions.IsReady()) { return false; }
	Actions.SetStamina(50.f);
	for (const FGameplayTag Tag : { MyGameplayTags::DK_Status_Dodging.GetTag(),
		MyGameplayTags::DK_Status_Attacking.GetTag(), MyGameplayTags::DK_Status_Defending.GetTag() })
	{
		Actions.ASC->AddLooseGameplayTag(Tag);
		Actions.TickFor(0.1f, true);
		TestEqual(TEXT("Movement during combat actions does not also charge sprint cost"), Actions.Stamina(), 50.f);
		Actions.ASC->RemoveLooseGameplayTag(Tag);
	}
	Actions.Character->AddActorWorldOffset(FVector(0.f, 0.f, 200.f));
	Actions.Movement->SetMovementMode(MOVE_Falling);
	Actions.TickFor(0.1f, true);
	TestEqual(TEXT("Airborne movement does not charge sprint cost"), Actions.Stamina(), 50.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstSprintRecoveryDelayTest, "First.Combat.SprintStamina.RecoveryDelayAndOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFirstSprintRecoveryDelayTest::RunTest(const FString& Parameters)
{
	FirstSprintStaminaTests::FFixture F(*this);
	if (!F.IsReady()) { return false; }
	F.AddRegen();
	F.SetStamina(50.f);
	F.TickFor(0.2f, true);
	const float AfterRun = F.Stamina();
	FFirstSprintStaminaTestAccess::Release(F.Character);
	F.TickFor(0.49f);
	TestEqual(TEXT("Releasing Shift does not bypass the half-second delay"), F.PauseCount(), 1);
	TestEqual(TEXT("No regeneration occurs during the stop delay"), F.Stamina(), AfterRun);
	F.TickFor(0.04f);
	TestEqual(TEXT("Sprint pause expires after half a second"), F.PauseCount(), 0);
	F.TickFor(0.55f);
	TestTrue(TEXT("The real periodic regeneration resumes"), F.Stamina() > AfterRun);

	// Re-enter through real input; wait for Dodge, then run briefly.
	FFirstSprintStaminaTestAccess::Press(F.Character);
	F.TickFor(0.4f);
	F.TickFor(0.1f, true);
	F.AddGuardPause();
	TestEqual(TEXT("Sprint and guard each own their pause"), F.PauseCount(), 2);
	F.Movement->DisableMovement();
	F.TickFor(0.55f);
	TestEqual(TEXT("Sprint pause expires even with no movement callbacks; guard pause remains"), F.PauseCount(), 1);
	F.TickFor(0.25f);
	TestEqual(TEXT("Guard's own duration then expires normally"), F.PauseCount(), 0);

	FirstSprintStaminaTests::FFixture Hitch(*this);
	if (!Hitch.IsReady()) { return false; }
	Hitch.AddRegen();
	Hitch.SetStamina(50.f);
	Hitch.AllowLongTestFrame();
	Hitch.TickFor(0.6f, true, 0.6f);
	TestEqual(TEXT("A long running frame does not expire its own regeneration pause"), Hitch.PauseCount(), 1);
	TestTrue(TEXT("A long running frame still charges elapsed time without regeneration"),
		FMath::IsNearlyEqual(Hitch.Stamina(), 47.f, 0.005f));
	Hitch.ASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_StaminaRegenPaused);
	Hitch.Character->PrepareForDeath();
	TestEqual(TEXT("Death cleanup releases only the sprint-owned pause"), Hitch.PauseCount(), 1);
	Hitch.TickFor(0.7f);
	TestEqual(TEXT("No stale sprint timer removes another owner's pause"), Hitch.PauseCount(), 1);
	Hitch.ASC->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_StaminaRegenPaused);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstSprintExhaustionTest, "First.Combat.SprintStamina.ExhaustionAndHeldInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFirstSprintExhaustionTest::RunTest(const FString& Parameters)
{
	FirstSprintStaminaTests::FFixture F(*this);
	if (!F.IsReady()) { return false; }
	F.SetStamina(1.f);
	F.TickFor(0.3f, true);
	TestEqual(TEXT("Running clamps the final partial cost to zero"), F.Stamina(), 0.f);
	TestEqual(TEXT("Running exhaustion shares the red-frame tag"), F.ExhaustionCount(), 1);
	TestEqual(TEXT("Exhaustion immediately restores walking speed"), F.Movement->MaxWalkSpeed, 250.f);
	TestFalse(TEXT("Running exhaustion also blocks the real Dodge ability"), F.TryDodge());
	F.TickFor(0.6f, true);
	TestEqual(TEXT("Holding movement and Shift cannot stack exhaustion or keep draining"), F.ExhaustionCount(), 1);
	TestEqual(TEXT("Recovery cannot deadlock on the sprint pause while Shift stays held"), F.PauseCount(), 0);
	F.SetStamina(99.f);
	TestEqual(TEXT("Partial recovery still permits only walking"), F.Movement->MaxWalkSpeed, 250.f);
	F.SetStamina(100.f);
	TestEqual(TEXT("Full recovery removes exhaustion"), F.ExhaustionCount(), 0);
	TestEqual(TEXT("Held input automatically resumes running after full recovery"), F.Movement->MaxWalkSpeed, 500.f);
	TestFalse(TEXT("Full recovery does not synthesize another Dodge press"), F.IsDodging());
	F.TickFor(0.1f, true);
	TestTrue(TEXT("Resumed running pays stamina again"), FMath::IsNearlyEqual(F.Stamina(), 99.5f, 0.005f));

	F.SetStamina(0.1f);
	F.TickFor(0.1f, true);
	FFirstSprintStaminaTestAccess::Release(F.Character);
	F.SetStamina(100.f);
	TestEqual(TEXT("Releasing Shift during recovery cancels automatic running"), F.Movement->MaxWalkSpeed, 250.f);
	return true;
}

#endif
