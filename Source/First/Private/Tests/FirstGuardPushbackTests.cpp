#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/DK/FirstGA_DKDodge.h"
#include "AbilitySystem/Abilities/DK/FirstGA_DKGuardBreak.h"
#include "AbilitySystem/Abilities/DK/FirstGA_DKGuardParry.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Character/BaseCharacter.h"
#include "Character/DKCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/Combat/DKDefenseComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "GameFramework/WorldSettings.h"
#include "MyGameplayTags.h"
#include "Types/FirstCombatTypes.h"

namespace FirstGuardPushbackTests
{

const FName PushbackName(TEXT("FirstGuardPushback"));

// Exercise Defense -> gameplay event -> real Guard ability -> CharacterMovement.
// Native characters and query collision boxes keep this independent of Blueprint assets.
class FFixture
{
public:
	FFixture(FAutomationTestBase& InTest, bool bAddWall = false)
		: Test(InTest), InitialFrameCounter(GFrameCounter)
	{
		if (!Test.TestNotNull(TEXT("The engine is available"), GEngine))
		{
			return;
		}
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!Test.TestNotNull(TEXT("The movement test world was created"), World))
		{
			return;
		}
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		// This asset-free world has no GameMode to route StartPlay. BeginPlay alone
		// does not start actors or register their component ticks in that case.
		World->GetWorldSettings()->NotifyBeginPlay();
		if (!Test.TestTrue(TEXT("The test world actually entered play"), World->GetBegunPlay()))
		{
			return;
		}

		if (!AddBox(FVector(0.f, 0.f, -50.f), FVector(2000.f, 2000.f, 50.f)))
		{
			return;
		}
		// The wall's front is x=-55: a radius-42 capsule has 13 cm of clearance.
		if (bAddWall && !AddBox(FVector(-65.f, 0.f, 150.f), FVector(10.f, 500.f, 200.f)))
		{
			return;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character = World->SpawnActor<ADKCharacter>(ADKCharacter::StaticClass(),
			FTransform(FVector(0.f, 0.f, 98.f)), SpawnParameters);
		Attacker = World->SpawnActor<ABaseCharacter>(ABaseCharacter::StaticClass(),
			FTransform(FVector(500.f, 0.f, 98.f)), SpawnParameters);
		if (!Test.TestNotNull(TEXT("The native defender was spawned"), Character) ||
			!Test.TestNotNull(TEXT("The native attacker was spawned"), Attacker))
		{
			return;
		}
		// A real ASC also receives the boss-side Parried event without a fake listener.
		Attacker->GetFirstAbilitySystemComponent()->InitAbilityActorInfo(Attacker, Attacker);
		Attacker->GetCharacterMovement()->DisableMovement();

		ASC = Character->GetFirstAbilitySystemComponent();
		Movement = Character->GetCharacterMovement();
		if (!Test.TestTrue(TEXT("The defender received BeginPlay"), Character->HasActorBegunPlay()) ||
			!Test.TestTrue(TEXT("CharacterMovement received BeginPlay"), Movement->HasBegunPlay()) ||
			!Test.TestTrue(TEXT("CharacterMovement tick is registered"), Movement->PrimaryComponentTick.IsTickFunctionRegistered()))
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

		FGameplayAbilitySpec GuardSpec(UFirstGA_DKGuardParry::StaticClass(), 1);
		GuardSpec.GetDynamicSpecSourceTags().AddTag(MyGameplayTags::InputTag_GuardParry);
		GuardHandle = ASC->GiveAbility(GuardSpec);
		DodgeHandle = ASC->GiveAbility(FGameplayAbilitySpec(UFirstGA_DKDodge::StaticClass(), 1));
		ASC->GiveAbility(FGameplayAbilitySpec(UFirstGA_DKGuardBreak::StaticClass(), 1));

		TickFor(0.05f);
		if (!Test.TestTrue(TEXT("The defender settles on the collision floor"), Movement->IsMovingOnGround()) ||
			!Test.TestTrue(TEXT("World ticks perform a real walkable-floor query"), Movement->CurrentFloor.IsWalkableFloor()))
		{
			return;
		}
		ASC->OnAbilityInputPressed(MyGameplayTags::InputTag_GuardParry);
		bReady = Test.TestTrue(TEXT("The real Guard ability accepts held input"), IsGuardActive()) &&
			Test.TestTrue(TEXT("Guard owns Blocking"), ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Blocking));
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
	FVector Position() const { return Character->GetActorLocation(); }
	bool TryDodge() { return ASC->TryActivateAbility(DodgeHandle, false); }
	bool IsGuardActive() const
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(GuardHandle);
		return Spec && Spec->IsActive();
	}

	EFirstDefenseResult Hit(const FFirstMeleeDefenseData& Attack)
	{
		return Character->GetDKDefenseComponent()->ResolveIncomingMeleeAttack(Attacker, Attack);
	}

	void TickFor(float Duration)
	{
		while (Duration > UE_SMALL_NUMBER)
		{
			const float Step = FMath::Min(Duration, 0.005f);
			World->Tick(LEVELTICK_All, Step);
			Duration -= Step;
			// Match the engine GAS test fixture: timers need a new frame for each substep.
			++GFrameCounter;
		}
	}

	int32 LivePushbackCount() const
	{
		int32 Count = 0;
		auto CountSources = [&Count](const TArray<TSharedPtr<FRootMotionSource>>& Sources)
		{
			for (const TSharedPtr<FRootMotionSource>& Source : Sources)
			{
				if (Source.IsValid() && Source->InstanceName == PushbackName &&
					!Source->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval) &&
					!Source->Status.HasFlag(ERootMotionSourceStatusFlags::Finished))
				{
					++Count;
				}
			}
		};
		CountSources(Movement->CurrentRootMotion.RootMotionSources);
		CountSources(Movement->CurrentRootMotion.PendingAddRootMotionSources);
		return Count;
	}

	UFirstAbilitySystemComponent* ASC = nullptr;
	ADKCharacter* Character = nullptr;
	UCharacterMovementComponent* Movement = nullptr;

private:
	bool AddBox(const FVector& Location, const FVector& Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Test.TestNotNull(TEXT("A collision actor was spawned"), Actor))
		{
			return false;
		}
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->AddInstanceComponent(Box);
		Actor->SetRootComponent(Box);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetGenerateOverlapEvents(false);
		Box->SetWorldLocation(Location);
		Box->RegisterComponent();
		return true;
	}

	FAutomationTestBase& Test;
	uint64 InitialFrameCounter;
	UWorld* World = nullptr;
	ABaseCharacter* Attacker = nullptr;
	FGameplayAbilitySpecHandle GuardHandle;
	FGameplayAbilitySpecHandle DodgeHandle;
	bool bReady = false;
};

FFirstMeleeDefenseData BlockAttack(float Distance = 30.f, float Duration = 0.12f)
{
	FFirstMeleeDefenseData Attack;
	Attack.bBlockable = true;
	Attack.bParryable = false;
	Attack.GuardStaminaDamage = 5.f;
	Attack.GuardPushbackDistance = Distance;
	Attack.GuardPushbackDuration = Duration;
	return Attack;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstGuardPushbackDistanceTest,
	"First.Combat.GuardPushback.DistanceAndDisabledParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstGuardPushbackDistanceTest::RunTest(const FString& Parameters)
{
	{
		FirstGuardPushbackTests::FFixture Fixture(*this);
		if (!Fixture.IsReady())
		{
			return false;
		}
		const FVector Start = Fixture.Position();
		TestTrue(TEXT("The attack resolves as an ordinary block"),
			Fixture.Hit(FirstGuardPushbackTests::BlockAttack()) == EFirstDefenseResult::Blocked);
		TestEqual(TEXT("One pushback is created by the real GuardHit event"), Fixture.LivePushbackCount(), 1);
		TestEqual(TEXT("Normal block still applies its stamina cost"),
			Fixture.ASC->GetNumericAttribute(UFirstAttributeSet::GetStaminaAttribute()), 95.f);
		Fixture.TickFor(0.20f);
		TestEqual(TEXT("CharacterMovement moves thirty cm away from the attacker"),
			Start.X - Fixture.Position().X, 30.0, 1.5);
		TestEqual(TEXT("There is no sideways drift"), Fixture.Position().Y, Start.Y, 0.1);
		TestTrue(TEXT("Pushback leaves the defender grounded"), Fixture.Movement->IsMovingOnGround());
		TestEqual(TEXT("The finished pushback is no longer live"), Fixture.LivePushbackCount(), 0);
		const FVector Stopped = Fixture.Position();
		Fixture.TickFor(0.15f);
		TestEqual(TEXT("Natural completion removes the residual horizontal slide"),
			(Fixture.Position() - Stopped).Size2D(), 0.0, 0.1);
	}

	for (const bool bDisableDistance : { true, false })
	{
		FirstGuardPushbackTests::FFixture Fixture(*this);
		if (!Fixture.IsReady())
		{
			return false;
		}
		const FVector Start = Fixture.Position();
		const FFirstMeleeDefenseData Attack = FirstGuardPushbackTests::BlockAttack(
			bDisableDistance ? 0.f : 30.f, bDisableDistance ? 0.12f : 0.f);
		TestTrue(TEXT("A disabled pushback still permits ordinary blocking"), Fixture.Hit(Attack) == EFirstDefenseResult::Blocked);
		TestEqual(TEXT("Zero distance or duration creates no pushback"), Fixture.LivePushbackCount(), 0);
		Fixture.TickFor(0.20f);
		TestEqual(TEXT("A disabled pushback causes no horizontal displacement"),
			(Fixture.Position() - Start).Size2D(), 0.0, 0.1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstGuardPushbackCollisionTest,
	"First.Combat.GuardPushback.WallCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstGuardPushbackCollisionTest::RunTest(const FString& Parameters)
{
	FirstGuardPushbackTests::FFixture Fixture(*this, true);
	if (!Fixture.IsReady())
	{
		return false;
	}
	const double StartX = Fixture.Position().X;
	TestTrue(TEXT("The attack is blocked in front of the wall"),
		Fixture.Hit(FirstGuardPushbackTests::BlockAttack(60.f)) == EFirstDefenseResult::Blocked);
	Fixture.TickFor(0.20f);
	const double Travel = StartX - Fixture.Position().X;
	TestTrue(TEXT("Pushback advances into the free space before the wall"), Travel > 5.0);
	TestTrue(TEXT("The wall prevents the requested sixty-cm displacement"), Travel < 15.0);
	const double CapsuleBack = Fixture.Position().X - Fixture.Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	TestTrue(TEXT("The capsule remains in front of the wall surface at x=-55"), CapsuleBack >= -55.5);
	TestTrue(TEXT("Wall contact does not launch the defender"), Fixture.Movement->IsMovingOnGround());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstGuardPushbackReplacementAndDodgeTest,
	"First.Combat.GuardPushback.ReplacementAndDodgeCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstGuardPushbackReplacementAndDodgeTest::RunTest(const FString& Parameters)
{
	FirstGuardPushbackTests::FFixture Fixture(*this);
	if (!Fixture.IsReady())
	{
		return false;
	}
	TestTrue(TEXT("The first hit is blocked"), Fixture.Hit(FirstGuardPushbackTests::BlockAttack()) == EFirstDefenseResult::Blocked);
	Fixture.TickFor(0.04f);
	const TSharedPtr<FRootMotionSource> FirstSource = Fixture.Movement->GetRootMotionSource(FirstGuardPushbackTests::PushbackName);
	if (!TestTrue(TEXT("The first pushback is moving the character"), FirstSource.IsValid()))
	{
		return false;
	}
	const double ReplacementStartX = Fixture.Position().X;
	TestTrue(TEXT("The next hit is also blocked"),
		Fixture.Hit(FirstGuardPushbackTests::BlockAttack(60.f)) == EFirstDefenseResult::Blocked);
	TestTrue(TEXT("The earlier source is retired before movement resumes"),
		FirstSource->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
	TestEqual(TEXT("Only the replacement pushback remains live"), Fixture.LivePushbackCount(), 1);
	Fixture.TickFor(0.20f);
	TestEqual(TEXT("Only the new sixty-cm displacement is completed"),
		ReplacementStartX - Fixture.Position().X, 60.0, 1.5);

	TestTrue(TEXT("A later block starts another pushback"),
		Fixture.Hit(FirstGuardPushbackTests::BlockAttack()) == EFirstDefenseResult::Blocked);
	Fixture.TickFor(0.025f);
	const TSharedPtr<FRootMotionSource> CancelledSource = Fixture.Movement->GetRootMotionSource(FirstGuardPushbackTests::PushbackName);
	if (!TestTrue(TEXT("The pushback to cancel exists"), CancelledSource.IsValid()))
	{
		return false;
	}
	const double DodgeStartX = Fixture.Position().X;
	TestTrue(TEXT("The real Dodge ability can cancel Guard during pushback"), Fixture.TryDodge());
	TestFalse(TEXT("Dodge ends the Guard ability"), Fixture.IsGuardActive());
	TestTrue(TEXT("Guard's old motion source is marked for removal"),
		CancelledSource->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
	TestEqual(TEXT("Dodge leaves no live guard pushback"), Fixture.LivePushbackCount(), 0);
	Fixture.TickFor(0.02f);
	TestTrue(TEXT("The old source's delayed cleanup does not swallow forward Dodge displacement"),
		Fixture.Position().X - DodgeStartX > 1.0);
	TestTrue(TEXT("Dodge retains forward velocity after source cleanup"), Fixture.Movement->Velocity.X > 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstGuardPushbackOutcomeExclusionsTest,
	"First.Combat.GuardPushback.ParryAndGuardBreakExclusions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstGuardPushbackOutcomeExclusionsTest::RunTest(const FString& Parameters)
{
	for (const bool bParry : { true, false })
	{
		FirstGuardPushbackTests::FFixture Fixture(*this);
		if (!Fixture.IsReady())
		{
			return false;
		}
		TestTrue(TEXT("An ordinary block starts the original pushback"),
			Fixture.Hit(FirstGuardPushbackTests::BlockAttack()) == EFirstDefenseResult::Blocked);
		Fixture.TickFor(0.025f);
		const TSharedPtr<FRootMotionSource> OldSource = Fixture.Movement->GetRootMotionSource(FirstGuardPushbackTests::PushbackName);
		if (!TestTrue(TEXT("An active pushback exists before the defense outcome changes"), OldSource.IsValid()))
		{
			return false;
		}
		const FVector BeforeOutcome = Fixture.Position();
		FFirstMeleeDefenseData Attack = FirstGuardPushbackTests::BlockAttack();
		Attack.bParryable = bParry;
		Attack.GuardStaminaDamage = bParry ? 5.f : 100.f;
		const EFirstDefenseResult Result = Fixture.Hit(Attack);
		TestTrue(TEXT("The actual defense component chooses the requested outcome"),
			Result == (bParry ? EFirstDefenseResult::Parried : EFirstDefenseResult::Blocked));
		if (!bParry)
		{
			TestTrue(TEXT("The breaking hit activates the actual GuardBreak ability"),
				Fixture.ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_GuardBroken));
			TestFalse(TEXT("GuardBreak cancels Guard"), Fixture.IsGuardActive());
		}
		TestTrue(TEXT("Parry or GuardBreak retires the prior pushback"),
			OldSource->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
		TestEqual(TEXT("Neither outcome creates a new live pushback"), Fixture.LivePushbackCount(), 0);
		Fixture.TickFor(0.025f);
		TestEqual(TEXT("Changing the defense outcome stops residual horizontal displacement"),
			(Fixture.Position() - BeforeOutcome).Size2D(), 0.0, 0.1);
		if (bParry)
		{
			TestTrue(TEXT("An ordinary block can still resolve during the successful-parry state"),
				Fixture.Hit(FirstGuardPushbackTests::BlockAttack()) == EFirstDefenseResult::Blocked);
			TestEqual(TEXT("That block does not push during the successful-parry state"), Fixture.LivePushbackCount(), 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstGuardPushbackLateFrameCancellationTest,
	"First.Combat.GuardPushback.LateFrameCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstGuardPushbackLateFrameCancellationTest::RunTest(const FString& Parameters)
{
	FirstGuardPushbackTests::FFixture Fixture(*this);
	if (!Fixture.IsReady())
	{
		return false;
	}
	TestTrue(TEXT("The initial block starts pushback"),
		Fixture.Hit(FirstGuardPushbackTests::BlockAttack()) == EFirstDefenseResult::Blocked);
	Fixture.TickFor(0.025f);
	if (!TestEqual(TEXT("The initial pushback is active"), Fixture.LivePushbackCount(), 1))
	{
		return false;
	}

	constexpr float Step = 0.005f;
	FRootMotionSourceGroup& Sources = Fixture.Movement->CurrentRootMotion;
	Sources.CleanUpInvalidRootMotion(Step, *Fixture.Character, *Fixture.Movement);
	// A hit can arrive after this frame's cleanup. Do not tick the world again:
	// the retired source must coexist with its replacement during accumulation.
	TestTrue(TEXT("A hit after source cleanup replaces the pushback"),
		Fixture.Hit(FirstGuardPushbackTests::BlockAttack(60.f)) == EFirstDefenseResult::Blocked);
	Sources.PrepareRootMotion(Step, *Fixture.Character, *Fixture.Movement, true);
	FVector ReplacementVelocity = FVector::ZeroVector;
	Sources.AccumulateOverrideRootMotionVelocity(Step, *Fixture.Character, *Fixture.Movement, ReplacementVelocity);
	Sources.AccumulateAdditiveRootMotionVelocity(Step, *Fixture.Character, *Fixture.Movement, ReplacementVelocity);
	TestEqual(TEXT("The retired source does not override the new five-hundred-cm/s pushback"),
		ReplacementVelocity.X, -500.0, 0.1);

	Fixture.ASC->OnAbilityInputReleased(MyGameplayTags::InputTag_GuardParry);
	TestEqual(TEXT("Actual input release retires every guard pushback"), Fixture.LivePushbackCount(), 0);
	// Still no cleanup: a removed source must not suppress movement from a later action.
	Sources.PrepareRootMotion(Step, *Fixture.Character, *Fixture.Movement, true);
	FVector FollowingActionVelocity(600.f, 0.f, 0.f);
	Sources.AccumulateOverrideRootMotionVelocity(Step, *Fixture.Character, *Fixture.Movement, FollowingActionVelocity);
	Sources.AccumulateAdditiveRootMotionVelocity(Step, *Fixture.Character, *Fixture.Movement, FollowingActionVelocity);
	TestEqual(TEXT("Late cancellation preserves the following action's horizontal velocity"),
		FollowingActionVelocity, FVector(600.f, 0.f, 0.f), 0.1f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
