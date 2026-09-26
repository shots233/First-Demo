#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/BOSS/GA_Boss_NormalAttack.h"
#include "AbilitySystem/Abilities/BOSS/GA_Boss_ThreeCombo.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "AIController.h"
#include "AISystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifyQueue.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Character/BossCharacter.h"
#include "Character/DKCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "MyGameplayTags.h"
#include "Notifies/AnimNotify_DKGameplayEvent.h"
#include "UObject/StrongObjectPtr.h"

namespace FirstBossPursuitTests
{

// Exercise the real native abilities and the configured BOSS attack montages.
// A generic AI controller supplies TargetActor without starting the real BT.
// Root motion is ignored here to isolate ordinary-attack lifecycle and damage.
// FirstBossDashFollowupTests covers independent chase with real root motion.
class FFixture
{
public:
	explicit FFixture(FAutomationTestBase& InTest)
		: Test(InTest), InitialFrameCounter(GFrameCounter)
	{
		UClass* BossClass = LoadClass<ABossCharacter>(nullptr, TEXT("/Game/Enemy/BP_Boss.BP_Boss_C"));
		const ABossCharacter* Defaults = BossClass ? BossClass->GetDefaultObject<ABossCharacter>() : nullptr;
		if (!Test.TestNotNull(TEXT("The real BOSS defaults load"), Defaults) || !GEngine) { return; }
		if (!Test.TestNotNull(TEXT("The real BOSS mesh is configured"), Defaults->GetMesh()->GetSkeletalMeshAsset()) ||
			!Test.TestNotNull(TEXT("The real normal montage is configured"), Defaults->GetNormalAttackMontage()) ||
			!Test.TestNotNull(TEXT("The real three-combo montage is configured"), Defaults->GetThreeComboMontage()))
		{
			return;
		}

		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		// CreateWorld only requests an AI system automatically for editor
		// worlds. Blackboard initialization explicitly requires one even when
		// this game-world fixture never runs a behavior tree or navigation.
		if (!Test.TestNotNull(TEXT("The isolated game world creates its AI system"), World->CreateAISystem())) { return; }
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		AddBox(FVector(0.f, 0.f, -50.f), FVector(2000.f, 2000.f, 50.f));

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Boss = World->SpawnActor<ABossCharacter>(ABossCharacter::StaticClass(),
			FTransform(FVector(0.f, 0.f, 98.f)), SpawnParameters);
		Target = World->SpawnActor<ADKCharacter>(ADKCharacter::StaticClass(),
			FTransform(FVector(200.f, 0.f, 98.f)), SpawnParameters);
		if (!Test.TestNotNull(TEXT("The native BOSS spawns"), Boss) ||
			!Test.TestNotNull(TEXT("The native target spawns"), Target)) { return; }

		Boss->NormalAttackMontage = Defaults->GetNormalAttackMontage();
		Boss->ThreeComboMontage = Defaults->GetThreeComboMontage();
		USkeletalMeshComponent* Mesh = Boss->GetMesh();
		Mesh->SetSkeletalMeshAsset(Defaults->GetMesh()->GetSkeletalMeshAsset());
		Mesh->SetRelativeTransform(Defaults->GetMesh()->GetRelativeTransform());
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		Anim = Mesh->GetAnimInstance();
		if (!Test.TestNotNull(TEXT("The native BOSS animation instance initializes"), Anim)) { return; }
		Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
		Boss->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		Boss->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Target->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		Target->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

		AAIController* Controller = World->SpawnActor<AAIController>();
		if (!Test.TestNotNull(TEXT("A generic AI controller spawns"), Controller)) { return; }
		BlackboardData.Reset(NewObject<UBlackboardData>(Controller));
		FBlackboardEntry Entry;
		Entry.EntryName = TEXT("TargetActor");
		UBlackboardKeyType_Object* ObjectKey = NewObject<UBlackboardKeyType_Object>(BlackboardData.Get());
		ObjectKey->BaseClass = AActor::StaticClass();
		Entry.KeyType = ObjectKey;
		BlackboardData->Keys.Add(Entry);
		BlackboardData->UpdateKeyIDs();
		Controller->Possess(Boss);
		if (!Test.TestTrue(TEXT("The generic AI initializes its target blackboard"),
			Controller->UseBlackboard(BlackboardData.Get(), Blackboard))) { return; }
		Blackboard->SetValueAsObject(TEXT("TargetActor"), Target);

		ASC = Boss->GetFirstAbilitySystemComponent();
		InitializeAttributes(Boss, ASC);
		TargetASC = Target->GetFirstAbilitySystemComponent();
		InitializeAttributes(Target, TargetASC);
		ASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_WeaponDrawn);
		NormalHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGA_Boss_NormalAttack::StaticClass(), 1));
		ThreeHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGA_Boss_ThreeCombo::StaticClass(), 1));
		AttackingTagHandle = ASC->RegisterGameplayTagEvent(MyGameplayTags::Boss_Status_Attacking,
			EGameplayTagEventType::NewOrRemoved).AddLambda([this](FGameplayTag, int32 Count)
		{
			if (Count == 0) { ++AttackingTagDrops; }
		});
		PursuitNotify.Reset(NewObject<UAnimNotify_DKGameplayEvent>(Boss));
		PursuitNotify->EventTag = MyGameplayTags::Boss_Event_PursuitPoint;
		TickFor(0.05f);
		bReady = true;
	}

	~FFixture()
	{
		if (ASC)
		{
			ASC->RegisterGameplayTagEvent(MyGameplayTags::Boss_Status_Attacking,
				EGameplayTagEventType::NewOrRemoved).Remove(AttackingTagHandle);
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
	ABossCharacter* GetBoss() const { return Boss; }
	UAnimMontage* NormalMontage() const { return Boss->GetNormalAttackMontage(); }
	UAnimMontage* ThreeMontage() const { return Boss->GetThreeComboMontage(); }
	UAnimMontage* SourceMontage() const { return bThreeSource ? ThreeMontage() : NormalMontage(); }
	int32 StartedCount() const { return StartedMontages.Num(); }
	int32 BusyDropCount() const { return AttackingTagDrops; }
	bool HasTag(FGameplayTag Tag) const { return ASC->HasMatchingGameplayTag(Tag); }
	bool IsActive() const
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(CurrentHandle);
		return Spec && Spec->IsActive();
	}
	bool Start(bool bThree)
	{
		bThreeSource = bThree;
		CurrentHandle = bThree ? ThreeHandle : NormalHandle;
		const bool bStarted = ASC->TryActivateAbility(CurrentHandle);
		ObserveMontage();
		return Test.TestTrue(TEXT("The requested native attack activates"), bStarted && IsActive());
	}
	void Cancel() { ASC->CancelAbilityHandle(CurrentHandle); }
	void Point(UAnimMontage* Montage = nullptr)
	{
		// Deliver the production notify with real source provenance; no ability
		// internals are invoked and no saved montage is modified.
		PursuitNotify->Notify(Boss->GetMesh(), Montage ? Montage : SourceMontage(), FAnimNotifyEventReference());
		ObserveMontage();
	}
	float TargetHealth() const { return TargetASC->GetNumericAttribute(UFirstAttributeSet::GetHealthAttribute()); }
	void HitTarget()
	{
		FGameplayEventData Payload;
		Payload.EventTag = MyGameplayTags::DK_Event_MeleeHit;
		Payload.Instigator = Boss;
		Payload.Target = Target;
		ASC->HandleGameplayEvent(Payload.EventTag, &Payload);
	}
	void EnableTargetParry()
	{
		Target->SetActorRotation((Boss->GetActorLocation() - Target->GetActorLocation()).Rotation());
		TargetASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_ParryWindow);
	}
	void AddWall() { AddBox(FVector(100.f, 0.f, 110.f), FVector(10.f, 100.f, 110.f)); }
	void TickFor(float Duration)
	{
		const int32 NumSteps = FMath::CeilToInt(Duration / 0.005f);
		if (NumSteps <= 0) { return; }
		const float DeltaSeconds = Duration / NumSteps;
		for (int32 Tick = 0; Tick < NumSteps; ++Tick)
		{
			World->Tick(LEVELTICK_All, DeltaSeconds);
			ObserveMontage();
			++GFrameCounter;
		}
	}
	void Finish()
	{
		TickFor(SourceMontage()->GetPlayLength() + NormalMontage()->GetPlayLength() + 1.f);
	}

private:
	static void InitializeAttributes(ABaseCharacter* Character, UFirstAbilitySystemComponent* Component)
	{
		Component->AddAttributeSetSubobject(Character->GetFirstAttributeSet());
		Component->InitAbilityActorInfo(Character, Character);
		Component->SetNumericAttributeBase(UFirstAttributeSet::GetMaxHealthAttribute(), 1000.f);
		Component->SetNumericAttributeBase(UFirstAttributeSet::GetHealthAttribute(), 1000.f);
		Component->SetNumericAttributeBase(UFirstAttributeSet::GetMaxStaminaAttribute(), 100.f);
		Component->SetNumericAttributeBase(UFirstAttributeSet::GetStaminaAttribute(), 100.f);
		Component->SetNumericAttributeBase(UFirstAttributeSet::GetMaxPoiseAttribute(), 100.f);
		Component->SetNumericAttributeBase(UFirstAttributeSet::GetPoiseAttribute(), 100.f);
		Component->SetNumericAttributeBase(UFirstAttributeSet::GetAttackPowerAttribute(), 1.f);
		Component->SetNumericAttributeBase(UFirstAttributeSet::GetDefensePowerAttribute(), 1.f);
	}
	void AddBox(const FVector& Location, const FVector& Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->AddInstanceComponent(Box);
		Actor->SetRootComponent(Box);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetWorldLocation(Location);
		Box->RegisterComponent();
	}
	void ObserveMontage()
	{
		UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		const FAnimMontageInstance* Instance = Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
		if (Instance && !StartedInstanceIDs.Contains(Instance->GetInstanceID()))
		{
			StartedInstanceIDs.Add(Instance->GetInstanceID());
			StartedMontages.Add(Montage);
		}
	}

	FAutomationTestBase& Test;
	uint64 InitialFrameCounter;
	UWorld* World = nullptr;
	ABossCharacter* Boss = nullptr;
	ADKCharacter* Target = nullptr;
	UFirstAbilitySystemComponent* ASC = nullptr;
	UFirstAbilitySystemComponent* TargetASC = nullptr;
	UAnimInstance* Anim = nullptr;
	UBlackboardComponent* Blackboard = nullptr;
	TStrongObjectPtr<UBlackboardData> BlackboardData;
	TStrongObjectPtr<UAnimNotify_DKGameplayEvent> PursuitNotify;
	TSet<int32> StartedInstanceIDs;
	TArray<UAnimMontage*> StartedMontages;
	FGameplayAbilitySpecHandle NormalHandle;
	FGameplayAbilitySpecHandle ThreeHandle;
	FGameplayAbilitySpecHandle CurrentHandle;
	FDelegateHandle AttackingTagHandle;
	int32 AttackingTagDrops = 0;
	bool bThreeSource = false;
	bool bReady = false;
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossAttackNoPursuitTest,
	"First.Combat.BossPursuit.OrdinaryAttacksNeverDerivePursuit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossAttackNoPursuitTest::RunTest(const FString& Parameters)
{
	for (bool bThree : { false, true })
	{
		for (bool bLegacyNotify : { false, true })
		{
			FirstBossPursuitTests::FFixture Fixture(*this);
			if (!Fixture.IsReady() || !Fixture.Start(bThree)) return false;
			TestEqual(TEXT("An ordinary attack starts one montage"), Fixture.StartedCount(), 1);
			TestTrue(TEXT("Each ordinary ability retains its own cooldown"), Fixture.HasTag(bThree
				? MyGameplayTags::Boss_Cooldown_Attack_ThreeCombo : MyGameplayTags::Boss_Cooldown_Attack_Normal));
			if (bLegacyNotify)
			{
				Fixture.Point();
				Fixture.Point();
				Fixture.TickFor(0.05f);
			}
			TestEqual(TEXT("An obsolete PursuitPoint notify cannot replace the original attack"), Fixture.StartedCount(), 1);
			Fixture.Finish();
			TestEqual(TEXT("Neither natural completion nor old notifies derive a pursuit"), Fixture.StartedCount(), 1);
			TestFalse(TEXT("The ordinary ability ends normally"), Fixture.IsActive());
			TestFalse(TEXT("Ordinary attacks never consume independent chase cooldown"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_Pursuit));
			TestFalse(TEXT("The ordinary attack releases Attacking"), Fixture.HasTag(MyGameplayTags::Boss_Status_Attacking));
			TestFalse(TEXT("The ordinary attack leaves no direction lock"), Fixture.HasTag(MyGameplayTags::Boss_Status_AttackDirectionLocked));
			TestEqual(TEXT("Attacking is released exactly once"), Fixture.BusyDropCount(), 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossAttackDamageAfterDetachTest,
	"First.Combat.BossPursuit.OrdinaryAttackDamageAndDefenseAfterDetach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossAttackDamageAfterDetachTest::RunTest(const FString& Parameters)
{
	for (bool bThree : { false, true })
	{
		FirstBossPursuitTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.Start(bThree)) return false;
		const float BeforeHit = Fixture.TargetHealth();
		Fixture.HitTarget();
		TestEqual(TEXT("The original ability retains its configured damage"), BeforeHit - Fixture.TargetHealth(),
			bThree ? Fixture.GetBoss()->GetThreeComboDamagePerHit() : Fixture.GetBoss()->GetNormalAttackDamage());
		Fixture.EnableTargetParry();
		const float BeforeParry = Fixture.TargetHealth();
		Fixture.HitTarget();
		TestEqual(TEXT("The original defense resolver still protects a successful parry"), Fixture.TargetHealth(), BeforeParry);
		Fixture.Point();
		Fixture.Finish();
		TestEqual(TEXT("Parry or an obsolete notify cannot add another attack"), Fixture.StartedCount(), 1);
		TestFalse(TEXT("The original attack lifecycle eventually ends"), Fixture.IsActive());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossAttackCancellationAfterDetachTest,
	"First.Combat.BossPursuit.OrdinaryAttackCancellationAfterDetach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossAttackCancellationAfterDetachTest::RunTest(const FString& Parameters)
{
	for (bool bThree : { false, true })
	{
		FirstBossPursuitTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.Start(bThree)) return false;
		Fixture.Point();
		Fixture.TickFor(0.05f);
		Fixture.Cancel();
		TestFalse(TEXT("Cancellation ends the detached ordinary ability immediately"), Fixture.IsActive());
		TestFalse(TEXT("Cancellation releases Attacking"), Fixture.HasTag(MyGameplayTags::Boss_Status_Attacking));
		Fixture.Finish();
		TestEqual(TEXT("Old notify or montage callbacks cannot resurrect an attack"), Fixture.StartedCount(), 1);
		TestEqual(TEXT("Cancellation releases Attacking once"), Fixture.BusyDropCount(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossPursuitUnsafeGroundTest,
	"First.Combat.BossPursuit.UnverifiedGroundTravelRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossPursuitUnsafeGroundTest::RunTest(const FString& Parameters)
{
	FirstBossPursuitTests::FFixture Fixture(*this);
	if (!Fixture.IsReady()) return false;
	ABossCharacter* Boss = Fixture.GetBoss();
	TestFalse(TEXT("Ground travel rejects negative distance"), Boss->HasSafeGroundTravel(FVector::ForwardVector, -1.f));
	TestFalse(TEXT("Ground travel rejects a zero travel direction"), Boss->HasSafeGroundTravel(FVector::ZeroVector, 200.f));
	TestFalse(TEXT("Ground travel rejects a negative landing tolerance"), Boss->HasSafeGroundTravel(FVector::ForwardVector, 200.f, 50.f, -1.f));
	Fixture.AddWall();
	TestFalse(TEXT("Ground travel cannot cross a blocking wall"), Boss->HasSafeGroundTravel(FVector::ForwardVector, 200.f));
	TestFalse(TEXT("A clear route still requires navigation data"), Boss->HasSafeGroundTravel(-FVector::ForwardVector, 200.f));
	return true;
}

#endif
