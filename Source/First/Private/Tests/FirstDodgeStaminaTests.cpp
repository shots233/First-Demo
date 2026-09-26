#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/DK/FirstGA_DKDodge.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "AbilitySystem/GameplayEffects/FirstGE_StaminaChange.h"
#include "Character/DKCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MyGameplayTags.h"

namespace FirstDodgeStaminaTests
{

// Use the real character, ASC, attribute callbacks and Dodge cost in an isolated world.
// Native defaults have no montage or startup data, so no project assets are loaded.
class FFixture
{
public:
	FFixture(FAutomationTestBase& InTest, float InitialStamina)
		: Test(InTest)
	{
		if (!Test.TestNotNull(TEXT("The engine is available"), GEngine))
		{
			return;
		}

		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!Test.TestNotNull(TEXT("A test world was created"), World))
		{
			return;
		}
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character = World->SpawnActor<ADKCharacter>(
			ADKCharacter::StaticClass(), FTransform::Identity, SpawnParameters);
		if (!Test.TestNotNull(TEXT("The native DK character was spawned"), Character))
		{
			return;
		}

		ASC = Character->GetFirstAbilitySystemComponent();
		Attributes = Character->GetFirstAttributeSet();
		if (!Test.TestNotNull(TEXT("The character owns its real ASC"), ASC) ||
			!Test.TestNotNull(TEXT("The character owns its real attribute set"), Attributes))
		{
			return;
		}
		ASC->AddAttributeSetSubobject(Attributes);
		ASC->InitAbilityActorInfo(Character, Character);
		ASC->SetNumericAttributeBase(UFirstAttributeSet::GetMaxStaminaAttribute(), 100.f);
		ASC->SetNumericAttributeBase(UFirstAttributeSet::GetStaminaAttribute(), InitialStamina);
		DodgeHandle = ASC->GiveAbility(FGameplayAbilitySpec(UFirstGA_DKDodge::StaticClass(), 1));
		bReady = Test.TestTrue(TEXT("The real Dodge ability was granted"), DodgeHandle.IsValid());
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
	}

	bool IsReady() const { return bReady; }
	bool TryDodge() { return ASC->TryActivateAbility(DodgeHandle, false); }
	void CancelDodge() { ASC->CancelAbilityHandle(DodgeHandle); }
	float Stamina() const { return Attributes->GetStamina(); }
	int32 ExhaustionCount() const { return ASC->GetGameplayTagCount(MyGameplayTags::DK_Status_DodgeExhausted); }

	bool IsDodgeActive() const
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(DodgeHandle);
		return Spec && Spec->IsActive();
	}

	bool ChangeStamina(float Delta)
	{
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
			UFirstGE_StaminaChange::StaticClass(), 1.f, ASC->MakeEffectContext());
		if (!Test.TestTrue(TEXT("A real stamina-change effect spec was created"), Spec.IsValid()))
		{
			return false;
		}
		Spec.Data->SetSetByCallerMagnitude(MyGameplayTags::Combat_SetByCaller_StaminaDelta, Delta);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		return true;
	}

	UFirstAbilitySystemComponent* ASC = nullptr;

private:
	FAutomationTestBase& Test;
	UWorld* World = nullptr;
	ADKCharacter* Character = nullptr;
	UFirstAttributeSet* Attributes = nullptr;
	FGameplayAbilitySpecHandle DodgeHandle;
	bool bReady = false;
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstDodgeStaminaEligibilityTest,
	"First.Combat.DodgeStamina.Eligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstDodgeStaminaEligibilityTest::RunTest(const FString& Parameters)
{
	FirstDodgeStaminaTests::FFixture Fixture(*this, 0.f);
	if (!Fixture.IsReady())
	{
		return false;
	}

	TestFalse(TEXT("Zero stamina rejects Dodge"), Fixture.TryDodge());
	TestFalse(TEXT("Rejected Dodge is inactive"), Fixture.IsDodgeActive());
	TestEqual(TEXT("Rejected Dodge does not create exhaustion"), Fixture.ExhaustionCount(), 0);
	TestEqual(TEXT("Rejected Dodge leaves stamina at zero"), Fixture.Stamina(), 0.f);

	if (!Fixture.ChangeStamina(50.f))
	{
		return false;
	}
	TestTrue(TEXT("Fifty stamina permits Dodge"), Fixture.TryDodge());
	TestTrue(TEXT("The accepted Dodge remains active"), Fixture.IsDodgeActive());
	TestEqual(TEXT("The real cost deducts twenty stamina"), Fixture.Stamina(), 30.f);
	TestEqual(TEXT("An affordable Dodge does not exhaust"), Fixture.ExhaustionCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstDodgeStaminaExhaustionPersistsTest,
	"First.Combat.DodgeStamina.ExhaustionPersists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstDodgeStaminaExhaustionPersistsTest::RunTest(const FString& Parameters)
{
	for (const float InitialStamina : { 10.f, 20.f })
	{
		FirstDodgeStaminaTests::FFixture Fixture(*this, InitialStamina);
		if (!Fixture.IsReady())
		{
			return false;
		}
		const FString Case = FString::Printf(TEXT("Starting at %.0f: "), InitialStamina);
		TestTrue(Case + TEXT("the last Dodge is accepted"), Fixture.TryDodge());
		TestEqual(Case + TEXT("the real cost is clamped to zero"), Fixture.Stamina(), 0.f);
		TestEqual(Case + TEXT("exhaustion is applied once"), Fixture.ExhaustionCount(), 1);
		TestTrue(Case + TEXT("exhaustion does not interrupt this Dodge"), Fixture.IsDodgeActive());
		TestTrue(Case + TEXT("Dodge still owns its active status"),
			Fixture.ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Dodging));

		Fixture.CancelDodge();
		TestFalse(Case + TEXT("cancellation ends Dodge"), Fixture.IsDodgeActive());
		TestFalse(Case + TEXT("cancellation removes the active Dodge status"),
			Fixture.ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Dodging));
		TestEqual(Case + TEXT("exhaustion survives the ability ending"), Fixture.ExhaustionCount(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstDodgeStaminaRecoveryAndRetryTest,
	"First.Combat.DodgeStamina.RecoveryAndRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstDodgeStaminaRecoveryAndRetryTest::RunTest(const FString& Parameters)
{
	FirstDodgeStaminaTests::FFixture Fixture(*this, 10.f);
	if (!Fixture.IsReady() || !TestTrue(TEXT("The exhausting Dodge is accepted"), Fixture.TryDodge()))
	{
		return false;
	}
	Fixture.CancelDodge();

	if (!Fixture.ChangeStamina(20.f))
	{
		return false;
	}
	TestFalse(TEXT("Recovering one normal Dodge cost is not enough to unlock"), Fixture.TryDodge());
	TestEqual(TEXT("A locked attempt does not deduct the recovered twenty"), Fixture.Stamina(), 20.f);
	TestEqual(TEXT("Exhaustion remains after partial recovery"), Fixture.ExhaustionCount(), 1);

	if (!Fixture.ChangeStamina(79.f))
	{
		return false;
	}
	TestFalse(TEXT("Ninety-nine stamina still rejects Dodge"), Fixture.TryDodge());
	TestEqual(TEXT("The rejected attempt leaves ninety-nine stamina"), Fixture.Stamina(), 99.f);
	TestEqual(TEXT("Almost full stamina remains locked"), Fixture.ExhaustionCount(), 1);

	if (!Fixture.ChangeStamina(1.f))
	{
		return false;
	}
	TestEqual(TEXT("The stamina effect reaches the actual maximum"), Fixture.Stamina(), 100.f);
	TestEqual(TEXT("Reaching full stamina removes exhaustion immediately"), Fixture.ExhaustionCount(), 0);
	TestFalse(TEXT("Recovery does not automatically start a Dodge"), Fixture.IsDodgeActive());
	TestTrue(TEXT("A new explicit attempt after full recovery succeeds"), Fixture.TryDodge());
	TestEqual(TEXT("The new Dodge pays its normal cost"), Fixture.Stamina(), 80.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstDodgeStaminaOtherDrainDoesNotLockTest,
	"First.Combat.DodgeStamina.OtherDrainDoesNotLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstDodgeStaminaOtherDrainDoesNotLockTest::RunTest(const FString& Parameters)
{
	FirstDodgeStaminaTests::FFixture Fixture(*this, 10.f);
	if (!Fixture.IsReady() || !Fixture.ChangeStamina(-10.f))
	{
		return false;
	}
	TestEqual(TEXT("A non-Dodge effect can drain stamina to zero"), Fixture.Stamina(), 0.f);
	TestEqual(TEXT("Other stamina drains do not create Dodge exhaustion"), Fixture.ExhaustionCount(), 0);
	TestFalse(TEXT("Zero still prevents Dodge without the exhaustion tag"), Fixture.TryDodge());
	TestEqual(TEXT("The zero-stamina attempt does not create exhaustion"), Fixture.ExhaustionCount(), 0);

	if (!Fixture.ChangeStamina(10.f))
	{
		return false;
	}
	TestTrue(TEXT("Partial recovery from a non-Dodge drain permits the last Dodge"), Fixture.TryDodge());
	TestEqual(TEXT("This actual Dodge then exhausts stamina"), Fixture.Stamina(), 0.f);
	TestEqual(TEXT("The Dodge payment now creates exhaustion"), Fixture.ExhaustionCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstDodgeStaminaTagAndMaxChangesTest,
	"First.Combat.DodgeStamina.TagAndMaxChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstDodgeStaminaTagAndMaxChangesTest::RunTest(const FString& Parameters)
{
	FirstDodgeStaminaTests::FFixture Fixture(*this, 10.f);
	if (!Fixture.IsReady() || !TestTrue(TEXT("The exhausting Dodge is accepted"), Fixture.TryDodge()))
	{
		return false;
	}
	Fixture.CancelDodge();
	Fixture.ASC->StartDodgeStaminaRecovery();
	Fixture.ASC->StartDodgeStaminaRecovery();
	TestEqual(TEXT("Repeated recovery requests never stack the lock tag"), Fixture.ExhaustionCount(), 1);

	// Zero is not a valid full-stamina threshold during initialization or stat changes.
	Fixture.ASC->SetNumericAttributeBase(UFirstAttributeSet::GetMaxStaminaAttribute(), 0.f);
	TestEqual(TEXT("A zero maximum does not unlock exhaustion"), Fixture.ExhaustionCount(), 1);
	Fixture.ASC->SetNumericAttributeBase(UFirstAttributeSet::GetMaxStaminaAttribute(), 100.f);
	if (!Fixture.ChangeStamina(50.f))
	{
		return false;
	}
	Fixture.ASC->SetNumericAttributeBase(UFirstAttributeSet::GetMaxStaminaAttribute(), 200.f);
	if (!Fixture.ChangeStamina(50.f))
	{
		return false;
	}
	TestEqual(TEXT("Recovery to the old maximum is no longer full"), Fixture.Stamina(), 100.f);
	TestEqual(TEXT("Increasing maximum stamina keeps the lock"), Fixture.ExhaustionCount(), 1);
	TestFalse(TEXT("A Dodge is rejected below the updated maximum"), Fixture.TryDodge());
	TestEqual(TEXT("The rejected attempt leaves stamina unchanged"), Fixture.Stamina(), 100.f);

	Fixture.ASC->SetNumericAttributeBase(UFirstAttributeSet::GetMaxStaminaAttribute(), 100.f);
	TestEqual(TEXT("A maximum change making stamina full unlocks immediately"), Fixture.ExhaustionCount(), 0);
	TestFalse(TEXT("Changing the maximum never triggers an automatic Dodge"), Fixture.IsDodgeActive());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
