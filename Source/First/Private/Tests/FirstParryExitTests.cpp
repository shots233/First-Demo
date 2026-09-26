#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/DK/FirstGA_DKGuardParry.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/DKCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/Combat/DKDefenseComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "MyGameplayTags.h"
#include "Notifies/ANS_ParryChainWindow.h"
#include "UObject/UnrealType.h"

namespace FirstParryExitTests
{

// Exercise ASC input and real montage advancement. Only the mesh and montage are
// borrowed from the player Blueprint; its initialization/UI graph is not run.
class FFixture
{
public:
	FFixture(FAutomationTestBase& InTest, float WindowDuration = 1.f)
		: Test(InTest), InitialFrameCounter(GFrameCounter)
	{
		UClass* PlayerClass = LoadClass<ADKCharacter>(nullptr,
			TEXT("/Game/DKCharacter/BP_DKCharacter.BP_DKCharacter_C"));
		const ADKCharacter* Defaults = PlayerClass ? PlayerClass->GetDefaultObject<ADKCharacter>() : nullptr;
		if (!Test.TestNotNull(TEXT("The configured player defaults load"), Defaults) || !GEngine)
		{
			return;
		}
		Montage = Defaults->GetGuardParryMontage();
		if (!Test.TestNotNull(TEXT("The real guard montage is configured"), Montage) ||
			!Test.TestNotNull(TEXT("The real player mesh is configured"), Defaults->GetMesh()->GetSkeletalMeshAsset()))
		{
			return;
		}
		for (const FName Section : {FName(TEXT("Start")), FName(TEXT("Loop")), FName(TEXT("Parry")), FName(TEXT("End"))})
		{
			if (!Test.TestTrue(TEXT("The montage contains its required sections"), Montage->IsValidSectionName(Section)))
			{
				return;
			}
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
		if (!Test.TestNotNull(TEXT("The native player was spawned"), Character))
		{
			return;
		}
		FObjectPropertyBase* MontageProperty = FindFProperty<FObjectPropertyBase>(ADKCharacter::StaticClass(), TEXT("GuardParryMontage"));
		FFloatProperty* DurationProperty = FindFProperty<FFloatProperty>(UDKDefenseComponent::StaticClass(), TEXT("ParryWindowDuration"));
		if (!Test.TestNotNull(TEXT("The existing montage property is available"), MontageProperty) ||
			!Test.TestNotNull(TEXT("The existing parry duration property is available"), DurationProperty))
		{
			return;
		}
		MontageProperty->SetObjectPropertyValue_InContainer(Character, Montage);
		DurationProperty->SetPropertyValue_InContainer(Character->GetDKDefenseComponent(), WindowDuration);
		USkeletalMeshComponent* Mesh = Character->GetMesh();
		Mesh->SetSkeletalMeshAsset(Defaults->GetMesh()->GetSkeletalMeshAsset());
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		Anim = Mesh->GetAnimInstance();
		if (!Test.TestNotNull(TEXT("The native animation instance was initialized"), Anim))
		{
			return;
		}

		ASC = Character->GetFirstAbilitySystemComponent();
		ASC->AddAttributeSetSubobject(Character->GetFirstAttributeSet());
		ASC->InitAbilityActorInfo(Character, Character);
		ASC->SetNumericAttributeBase(UFirstAttributeSet::GetMaxStaminaAttribute(), 100.f);
		ASC->SetNumericAttributeBase(UFirstAttributeSet::GetStaminaAttribute(), 100.f);
		Character->GetDKCombatComponent()->CurrentEquippedWeaponTag = MyGameplayTags::DK_Weapon_Sword;
		Character->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		FGameplayAbilitySpec GuardSpec(UFirstGA_DKGuardParry::StaticClass(), 1);
		GuardSpec.GetDynamicSpecSourceTags().AddTag(MyGameplayTags::InputTag_GuardParry);
		GuardHandle = ASC->GiveAbility(GuardSpec);
		TickFor(0.05f);
		Press();
		bReady = Test.TestTrue(TEXT("Guard activates through real ASC input"), IsGuardActive()) &&
			Test.TestTrue(TEXT("The real montage starts playing"), Anim->Montage_IsPlaying(Montage));
		if (FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage))
		{
			// The instance delegate observes every transition, including multiple
			// transitions in one tick; the ability uses the separate global broadcast.
			Instance->OnMontageSectionChanged.BindLambda([this](UAnimMontage*, FName SectionName, bool)
			{
				VisitedSections.Add(SectionName);
			});
		}
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
	bool IsGuardActive() const
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(GuardHandle);
		return Spec && Spec->IsActive();
	}
	bool HasWindow() const { return ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_ParryWindow); }
	bool IsMontageActive() const { return Anim->Montage_IsActive(Montage); }
	float ParryLength() const { return Montage->GetSectionLength(Montage->GetSectionIndex(TEXT("Parry"))); }
	bool InstanceAutoBlendOut() const
	{
		const FAnimMontageInstance* Instance = Anim->GetInstanceForMontage(Montage);
		return Instance && Instance->bEnableAutoBlendOut;
	}
	void Cancel() { ASC->CancelAbilityHandle(GuardHandle); }
	FName Section() const { return Anim->Montage_GetCurrentSection(Montage); }
	FName ParryExit() const
	{
		return Montage->GetSectionName(Anim->Montage_GetNextSectionID(Montage, Montage->GetSectionIndex(TEXT("Parry"))));
	}
	void Press() { ASC->OnAbilityInputPressed(MyGameplayTags::InputTag_GuardParry); }
	void Release() { ASC->OnAbilityInputReleased(MyGameplayTags::InputTag_GuardParry); }
	void Success()
	{
		VisitedSections.Reset();
		FGameplayEventData Event;
		Event.EventTag = MyGameplayTags::DK_Event_ParrySuccess;
		ASC->HandleGameplayEvent(Event.EventTag, &Event);
	}
	void OpenChainWindow()
	{
		// A native AnimInstance has no UpperBody slot graph. Inject only the ANS
		// event; input buffering, section jumps and playback remain the real path.
		FGameplayEventData Event;
		Event.EventTag = MyGameplayTags::DK_Event_ParryChainWindow_Open;
		for (const FAnimNotifyEvent& Notify : Montage->Notifies)
		{
			if (UANS_ParryChainWindow* ChainNotify = Cast<UANS_ParryChainWindow>(Notify.NotifyStateClass))
			{
				Event.OptionalObject = ChainNotify;
				break;
			}
		}
		if (!Test.TestNotNull(TEXT("The actual montage contains the chain notify source"), Event.OptionalObject.Get()))
		{
			return;
		}
		Event.OptionalObject2 = Montage;
		ASC->HandleGameplayEvent(Event.EventTag, &Event);
	}
	void TickFor(float Duration)
	{
		while (Duration > UE_SMALL_NUMBER)
		{
			const float Step = FMath::Min(Duration, 0.005f);
			World->Tick(LEVELTICK_All, Step);
			if (Anim)
			{
				VisitedSections.Add(Section());
			}
			Duration -= Step;
			++GFrameCounter;
		}
	}

	TSet<FName> VisitedSections;

private:
	FAutomationTestBase& Test;
	uint64 InitialFrameCounter;
	UWorld* World = nullptr;
	ADKCharacter* Character = nullptr;
	UFirstAbilitySystemComponent* ASC = nullptr;
	UAnimInstance* Anim = nullptr;
	UAnimMontage* Montage = nullptr;
	FGameplayAbilitySpecHandle GuardHandle;
	bool bReady = false;
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstParryExitReleasedTest,
	"First.Combat.ParryExit.ReleasedInputSkipsGuardLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstParryExitReleasedTest::RunTest(const FString& Parameters)
{
	for (const bool bReleaseBeforeSuccess : {true, false})
	{
		FirstParryExitTests::FFixture Fixture(*this);
		if (!Fixture.IsReady())
		{
			return false;
		}
		Fixture.TickFor(0.02f);
		if (bReleaseBeforeSuccess)
		{
			Fixture.Release();
		}
		Fixture.Success();
		TestEqual(TEXT("Success starts the actual Parry section"), Fixture.Section(), FName(TEXT("Parry")));
		if (!bReleaseBeforeSuccess)
		{
			TestEqual(TEXT("Held input initially routes back to guard"), Fixture.ParryExit(), FName(TEXT("Loop")));
			Fixture.TickFor(0.1f);
			Fixture.Release();
		}
		TestEqual(TEXT("Released input chooses natural completion at the Parry boundary"), Fixture.ParryExit(), NAME_None);
		Fixture.TickFor(0.2f);
		TestEqual(TEXT("Releasing does not interrupt the successful parry animation"), Fixture.Section(), FName(TEXT("Parry")));
		TestTrue(TEXT("Releasing preserves the one-second judgement window"), Fixture.HasWindow());
		Fixture.TickFor(1.8f);
		TestFalse(TEXT("Playback never enters the Block_Out section that starts with a raised sword"), Fixture.VisitedSections.Contains(TEXT("End")));
		TestFalse(TEXT("Playback never returns to the ordinary guard loop"), Fixture.VisitedSections.Contains(TEXT("Loop")));
		TestFalse(TEXT("The guard ability finishes after the Parry pose fades out"), Fixture.IsGuardActive());
		TestFalse(TEXT("The expired parry tag is removed"), Fixture.HasWindow());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstParryExitHeldAndChainTest,
	"First.Combat.ParryExit.HeldInputAndBufferedChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstParryExitHeldAndChainTest::RunTest(const FString& Parameters)
{
	{
		FirstParryExitTests::FFixture Fixture(*this);
		if (!Fixture.IsReady())
		{
			return false;
		}
		Fixture.Success();
		TestEqual(TEXT("Continuously held input keeps the guard exit"), Fixture.ParryExit(), FName(TEXT("Loop")));
		Fixture.TickFor(1.3f);
		TestEqual(TEXT("Held input naturally returns to the guard loop"), Fixture.Section(), FName(TEXT("Loop")));
		TestTrue(TEXT("Ordinary guarding regains normal automatic blend-out"), Fixture.InstanceAutoBlendOut());
		TestTrue(TEXT("Held guard remains active"), Fixture.IsGuardActive());
		Fixture.Release();
		Fixture.TickFor(0.8f);
		TestFalse(TEXT("Releasing after the success animation finishes guard"), Fixture.IsGuardActive());
	}
	{
		FirstParryExitTests::FFixture Fixture(*this);
		if (!Fixture.IsReady())
		{
			return false;
		}
		Fixture.Release();
		Fixture.Success();
		Fixture.TickFor(0.02f);
		Fixture.Press();
		TestEqual(TEXT("A fresh press during success restores the held exit"), Fixture.ParryExit(), FName(TEXT("Loop")));
		Fixture.Release();
		Fixture.OpenChainWindow();
		TestEqual(TEXT("A buffered tap takes the real montage back to Start"), Fixture.Section(), FName(TEXT("Start")));
		Fixture.TickFor(0.95f);
		TestTrue(TEXT("The consumed tap retains its full one-second window after release"), Fixture.HasWindow());
		Fixture.TickFor(0.1f);
		TestFalse(TEXT("The consumed tap expires once without extending on hold"), Fixture.HasWindow());
		Fixture.TickFor(0.8f);
		TestFalse(TEXT("The released chained guard eventually finishes"), Fixture.IsGuardActive());
	}
	{
		FirstParryExitTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) { return false; }
		Fixture.Release();
		Fixture.Success();
		Fixture.TickFor(Fixture.ParryLength() - 0.1f);
		TestTrue(TEXT("Parry remains active in the last 0.1 seconds instead of blending early"), Fixture.IsMontageActive());
		TestEqual(TEXT("The tail still plays Parry itself"), Fixture.Section(), FName(TEXT("Parry")));
		Fixture.OpenChainWindow();
		Fixture.Press();
		Fixture.Release();
		TestEqual(TEXT("A tail-window tap successfully restarts the real montage"), Fixture.Section(), FName(TEXT("Start")));
		TestTrue(TEXT("Consuming the chain restores ordinary blend-out settings"), Fixture.InstanceAutoBlendOut());
		Fixture.TickFor(0.95f);
		TestTrue(TEXT("The late chain owns its complete new one-second window"), Fixture.HasWindow());
		Fixture.TickFor(0.9f);
		TestFalse(TEXT("No old completion check interferes with or retains the chained ability"), Fixture.IsGuardActive());
	}
	{
		FirstParryExitTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) { return false; }
		Fixture.Release();
		Fixture.Success();
		Fixture.TickFor(0.2f);
		Fixture.Cancel();
		TestTrue(TEXT("Cancellation restores the exact montage instance's auto blend-out"), Fixture.InstanceAutoBlendOut());
		Fixture.TickFor(1.5f);
		TestFalse(TEXT("Cancellation leaves no delayed parry completion or live ability"), Fixture.IsGuardActive());
		TestFalse(TEXT("Cancellation still clears the parry judgement"), Fixture.HasWindow());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstParryExitLongWindowTest,
	"First.Combat.ParryExit.LongWindowOutlivesAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstParryExitLongWindowTest::RunTest(const FString& Parameters)
{
	FirstParryExitTests::FFixture Fixture(*this, 3.f);
	if (!Fixture.IsReady())
	{
		return false;
	}
	Fixture.Release();
	Fixture.Success();
	Fixture.TickFor(2.f);
	TestFalse(TEXT("Long judgement never replays the raised-sword lowering section"), Fixture.VisitedSections.Contains(TEXT("End")));
	TestFalse(TEXT("The actual montage has finished before the long judgement window"), Fixture.IsMontageActive());
	TestFalse(TEXT("A long judgement window does not insert a guard loop"), Fixture.VisitedSections.Contains(TEXT("Loop")));
	TestTrue(TEXT("The ability stays alive to honor the accepted long window"), Fixture.IsGuardActive());
	TestTrue(TEXT("Animation completion does not shorten the accepted window"), Fixture.HasWindow());
	Fixture.TickFor(0.95f);
	TestTrue(TEXT("The window remains valid immediately before its deadline"), Fixture.HasWindow());
	Fixture.TickFor(0.1f);
	TestFalse(TEXT("The timer clears the tag even after natural Parry completion"), Fixture.HasWindow());
	TestFalse(TEXT("A completed montage and expired window fully end the ability"), Fixture.IsGuardActive());
	return true;
}

#endif
