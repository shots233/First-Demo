#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/BOSS/FirstBossPursuitAbility.h"
#include "AbilitySystem/Abilities/BOSS/GA_Boss_NormalAttack.h"
#include "AbilitySystem/Abilities/BOSS/GA_Boss_ThreeCombo.h"
#include "AbilitySystem/Abilities/BOSS/GA_Boss_HitReact.h"
#include "AbilitySystem/Abilities/BOSS/GA_Boss_ParryStagger.h"
#include "AbilitySystem/Abilities/BOSS/GA_Boss_Executable.h"
#include "AbilitySystem/Abilities/DK/FirstGA_DKLightAttack.h"
#include "AbilitySystem/Abilities/FirstGameplayAbility.h"
#include "AbilitySystem/GameplayEffects/FirstGE_Damage.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "AIController.h"
#include "AISystem.h"
#include "AI/NavigationSystemBase.h"
#include "AI/Decorators/BTDecorator_BossCanPursue.h"
#include "AI/Decorators/BTDecorator_BossAttackCooldown.h"
#include "AI/Tasks/BTTask_ActivateAbilityAndWait.h"
#include "AI/Tasks/BTTask_BossPrepareRetaliation.h"
#include "AI/Tasks/BTTask_BossSelectAttack.h"
#include "AI/Tasks/BTTask_BossActivateAbilityByTag.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/AnimSequence.h"
#include "AnimNotifyState_MotionWarping.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "Character/BossCharacter.h"
#include "Character/DKCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/Combat/FirstBossRetaliationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "MyGameplayTags.h"
#include "MotionWarpingComponent.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "Notifies/AnimNotify_DKGameplayEvent.h"
#include "RootMotionModifier.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace FirstBossDashFollowupTests
{

// Unlike the ordinary-attack fixture, this world builds a real Recast
// navigation surface and runs ABP_Boss, CharacterMovement and animation root
// motion. No route checks or private ability phases are bypassed.
class FFixture
{
public:
	explicit FFixture(FAutomationTestBase& InTest)
		: Test(InTest), InitialFrameCounter(GFrameCounter)
	{
		UClass* BossClass = LoadClass<ABossCharacter>(nullptr, TEXT("/Game/Enemy/BP_Boss.BP_Boss_C"));
		const ABossCharacter* Defaults = BossClass ? BossClass->GetDefaultObject<ABossCharacter>() : nullptr;
		UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Enemy/AnimBP/ABP_Boss.ABP_Boss_C"));
		DodgeMontage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Enemy/AnimBP/Montages/AM_Boss_Dodge_F.AM_Boss_Dodge_F"));
		if (!Test.TestNotNull(TEXT("Saved BOSS defaults load"), Defaults) ||
			!Test.TestNotNull(TEXT("The real BOSS animation blueprint loads"), AnimClass) ||
			!Test.TestNotNull(TEXT("The saved root-motion dodge montage loads"), DodgeMontage) || !GEngine) return;
		if (!Test.TestNotNull(TEXT("The real BOSS mesh is configured"), Defaults->GetMesh()->GetSkeletalMeshAsset()) ||
			!Test.TestNotNull(TEXT("The real normal attack is configured"), Defaults->GetNormalAttackMontage())) return;
		WaitForMontageCompression(DodgeMontage);
		WaitForMontageCompression(Defaults->GetNormalAttackMontage());
		const FVector RawTravel = DodgeMontage->ExtractRootMotionFromTrackRange(0.f, DodgeMontage->GetPlayLength(),
			FAnimExtractContext(0.0, true)).GetTranslation();
		if (!Test.TestTrue(TEXT("The saved dodge has usable compressed root displacement"), RawTravel.Size2D() > 100.f)) return;

		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		if (!Test.TestNotNull(TEXT("The isolated game world creates its AI system"), World->CreateAISystem())) return;
		FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::GameMode, nullptr, false);
		UNavigationSystemV1* Navigation = UNavigationSystemV1::GetCurrent(World);
		if (!Test.TestNotNull(TEXT("A real navigation system is installed"), Navigation)) return;
		AddFloor();
		ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
		if (!Test.TestNotNull(TEXT("The fixture creates navigation bounds"), Bounds)) return;
		// NavigationSystem uses all component bounds; a transient shape is enough
		// to define them without an editor brush builder or a saved map change.
		UBoxComponent* BoundsShape = NewObject<UBoxComponent>(Bounds);
		Bounds->AddInstanceComponent(BoundsShape);
		BoundsShape->SetupAttachment(Bounds->GetRootComponent());
		BoundsShape->SetMobility(EComponentMobility::Static);
		BoundsShape->SetBoxExtent(FVector(1800.f, 1800.f, 300.f));
		BoundsShape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoundsShape->SetCanEverAffectNavigation(false);
		BoundsShape->RegisterComponent();
		ARecastNavMesh* NavData = World->SpawnActor<ARecastNavMesh>();
		if (!Test.TestNotNull(TEXT("The fixture creates real Recast navigation data"), NavData)) return;
		// Set the same editable instance option exposed by RecastNavMesh in UE.
		// No navigation-system or gameplay internals are altered.
		FProperty* GenerationProperty = FindFProperty<FProperty>(NavData->GetClass(), TEXT("RuntimeGeneration"));
		if (!Test.TestNotNull(TEXT("Recast exposes Runtime Generation configuration"), GenerationProperty)) return;
		GenerationProperty->ImportText_Direct(TEXT("Dynamic"), GenerationProperty->ContainerPtrToValuePtr<void>(NavData),
			NavData, PPF_None);
		if (!Test.TestEqual(TEXT("This fixture uses dynamic navigation generation"),
			NavData->GetRuntimeGenerationMode(), ERuntimeGenerationType::Dynamic)) return;
		Navigation->OnWorldInitDone(FNavigationSystemRunMode::GameMode);
		Navigation->Build();
		FNavLocation FloorPoint;
		if (!Test.TestTrue(TEXT("The test floor has actual generated navigation polygons"),
			Navigation->ProjectPointToNavigation(FVector(300.f, 0.f, 0.f), FloorPoint, FVector(30.f, 30.f, 100.f)))) return;

		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Boss = World->SpawnActor<ABossCharacter>(ABossCharacter::StaticClass(),
			FTransform(FVector(0.f, 0.f, 98.f)), SpawnParameters);
		Target = World->SpawnActor<ADKCharacter>(ADKCharacter::StaticClass(),
			FTransform(FVector(550.f, 0.f, 98.f)), SpawnParameters);
		if (!Test.TestNotNull(TEXT("The native BOSS spawns"), Boss) ||
			!Test.TestNotNull(TEXT("The native moving target spawns"), Target)) return;
		Boss->NormalAttackMontage = Defaults->GetNormalAttackMontage();
		Boss->ThreeComboMontage = Defaults->GetThreeComboMontage();
		WaitForMontageCompression(Boss->ThreeComboMontage);
		Boss->PursuitSettings.ForwardDodgeMontage = DodgeMontage;
		Boss->PursuitSettings.ForwardDodgeAnimation = nullptr;
		Boss->PursuitSettings.FollowupAttackMontage = nullptr;
		Boss->PursuitSettings.DodgePlayRate = 1.5f;
		USkeletalMeshComponent* Mesh = Boss->GetMesh();
		Mesh->SetSkeletalMeshAsset(Defaults->GetMesh()->GetSkeletalMeshAsset());
		Mesh->SetRelativeTransform(Defaults->GetMesh()->GetRelativeTransform());
		// This native actor was already initialized before its saved BP mesh
		// transform was copied. Refresh the same offsets that Character caches
		// after construction in game; Motion Warping uses these, not just Mesh.
		Boss->CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Mesh->SetAnimInstanceClass(AnimClass);
		Anim = Mesh->GetAnimInstance();
		if (!Test.TestNotNull(TEXT("The real BOSS animation instance initializes"), Anim)) return;
		Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
		Boss->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		Boss->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Target->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		Target->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Target->GetCharacterMovement()->MaxAcceleration = 100000.f;

		AAIController* Controller = World->SpawnActor<AAIController>();
		if (!Test.TestNotNull(TEXT("A generic controller supplies a target without running the BOSS behavior tree"), Controller)) return;
		BlackboardData.Reset(NewObject<UBlackboardData>(Controller));
		FBlackboardEntry Entry;
		Entry.EntryName = TEXT("TargetActor");
		UBlackboardKeyType_Object* Key = NewObject<UBlackboardKeyType_Object>(BlackboardData.Get());
		Key->BaseClass = AActor::StaticClass();
		Entry.KeyType = Key;
		BlackboardData->Keys.Add(Entry);
		FBlackboardEntry AttackTagEntry;
		AttackTagEntry.EntryName = TEXT("AttackTag");
		AttackTagEntry.KeyType = NewObject<UBlackboardKeyType_Name>(BlackboardData.Get());
		BlackboardData->Keys.Add(AttackTagEntry);
		for (const FName KeyName : { FName(TEXT("bShouldChase")), FName(TEXT("bPlayerTooFar")),
			FName(TEXT("bIsBusy")), FName(TEXT("bProvokedByDamage")), FName(TEXT("bTargetNavigationBlocked")) })
		{
			FBlackboardEntry BoolEntry;
			BoolEntry.EntryName = KeyName;
			BoolEntry.KeyType = NewObject<UBlackboardKeyType_Bool>(BlackboardData.Get());
			BlackboardData->Keys.Add(BoolEntry);
		}
		BlackboardData->UpdateKeyIDs();
		Controller->Possess(Boss);
		if (!Test.TestTrue(TEXT("The controller initializes its blackboard"), Controller->UseBlackboard(BlackboardData.Get(), Blackboard))) return;
		Blackboard->SetValueAsObject(TEXT("TargetActor"), Target);
		Blackboard->SetValueAsBool(TEXT("bShouldChase"), true);
		Blackboard->SetValueAsBool(TEXT("bPlayerTooFar"), true);
		ASC = Boss->GetFirstAbilitySystemComponent();
		InitializeAttributes(Boss, ASC);
		TargetASC = Target->GetFirstAbilitySystemComponent();
		InitializeAttributes(Target, TargetASC);
		ASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_WeaponDrawn);
		NormalHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGA_Boss_NormalAttack::StaticClass(), 1));
		ThreeHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGA_Boss_ThreeCombo::StaticClass(), 1));
		PursuitHandle = ASC->GiveAbility(FGameplayAbilitySpec(UFirstBossPursuitAbility::StaticClass(), 1));
		AttackingTagHandle = ASC->RegisterGameplayTagEvent(MyGameplayTags::Boss_Status_Attacking,
			EGameplayTagEventType::NewOrRemoved).AddLambda([this](FGameplayTag, int32 Count)
		{
			if (Count == 0) ++AttackingTagDrops;
		});
		TickFor(0.05f);
		bReady = Test.TestTrue(TEXT("The production route check accepts this navigable clear floor"),
			Boss->HasSafeGroundTravel(FVector::ForwardVector, 350.f));
	}

	~FFixture()
	{
		if (Boss)
		{
			if (const AAIController* Controller = Cast<AAIController>(Boss->GetController()))
			{
				if (UBehaviorTreeComponent* Tree = Cast<UBehaviorTreeComponent>(Controller->GetBrainComponent()))
					Tree->StopTree(EBTStopMode::Forced);
			}
		}
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
	ADKCharacter* GetTarget() const { return Target; }
	int32 BusyDrops() const { return AttackingTagDrops; }
	int32 StartedCount() const { return StartedMontages.Num(); }
	UAnimMontage* LastMontage() const { return StartedMontages.IsEmpty() ? nullptr : StartedMontages.Last(); }
	bool UseSavedCustomFollowup()
	{
		Boss->PursuitSettings.FollowupAttackMontage = LoadObject<UAnimMontage>(nullptr,
			TEXT("/Game/Enemy/AnimBP/Montages/Attacks/AM_Boss_Attack_stinger.AM_Boss_Attack_stinger"));
		if (!Test.TestNotNull(TEXT("The user's currently selected custom followup loads"), Boss->PursuitSettings.FollowupAttackMontage.Get())) return false;
		WaitForMontageCompression(Boss->PursuitSettings.FollowupAttackMontage);
		return true;
	}
	int32 NormalCount() const { return StartedMontages.FilterByPredicate([this](UAnimMontage* Montage) { return Montage == Boss->NormalAttackMontage; }).Num(); }
	int32 DodgeCount() const { return StartedMontages.FilterByPredicate([this](UAnimMontage* Montage) { return Montage == DodgeMontage; }).Num(); }
	bool IsActive() const
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(PursuitHandle);
		return Spec && Spec->IsActive();
	}
	bool CanActivate() const
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(PursuitHandle);
		const FGameplayEventData Event = ChaseEvent();
		return Spec && Spec->Ability->CanActivateAbility(PursuitHandle, ASC->AbilityActorInfo.Get()) &&
			Spec->Ability->ShouldAbilityRespondToEvent(ASC->AbilityActorInfo.Get(), &Event);
	}
	FGameplayEventData ChaseEvent() const
	{
		FGameplayEventData Event;
		Event.EventTag = MyGameplayTags::Boss_Ability_Attack_Pursuit;
		Event.Instigator = Boss;
		Event.Target = Cast<AActor>(Blackboard->GetValueAsObject(TEXT("TargetActor")));
		return Event;
	}
	bool TryStart()
	{
		const FGameplayEventData Event = ChaseEvent();
		return ASC->TriggerAbilityFromGameplayEvent(PursuitHandle, ASC->AbilityActorInfo.Get(),
			Event.EventTag, &Event, *ASC);
	}
	bool CanChase()
	{
		if (!PolicyComponent.IsValid())
		{
			AAIController* Controller = CastChecked<AAIController>(Boss->GetController());
			PolicyComponent.Reset(NewObject<UBehaviorTreeComponent>(Controller));
			PolicyComponent->RegisterComponent();
			PolicyComponent->CacheBlackboardComponent(Blackboard);
			PolicyTree.Reset(NewObject<UBehaviorTree>(Controller));
			PolicyTree->BlackboardAsset = BlackboardData.Get();
			PolicyCondition.Reset(NewObject<UBTDecorator_BossCanPursue>(PolicyTree.Get()));
			// This isolated query exercises the production predicate without
			// starting an animation. The integration tests below use real node
			// instancing, tick notifications and lower-priority branch aborts.
			PolicyCondition->ForceInstancing(false);
			static_cast<UBTNode*>(PolicyCondition.Get())->InitializeFromAsset(*PolicyTree);
		}
		return PolicyCondition->WrappedCanExecute(*PolicyComponent, nullptr);
	}
	bool HasTag(FGameplayTag Tag) const { return ASC->HasMatchingGameplayTag(Tag); }
	void SetBossTag(FGameplayTag Tag, bool bSet)
	{
		if (bSet) ASC->AddLooseGameplayTag(Tag);
		else ASC->RemoveLooseGameplayTag(Tag);
	}
	void SetBlackboardBool(FName Key, bool bValue) { Blackboard->SetValueAsBool(Key, bValue); }
	bool RunChaseTree()
	{
		AAIController* Controller = CastChecked<AAIController>(Boss->GetController());
		ChaseTree.Reset(NewObject<UBehaviorTree>(Controller));
		ChaseTree->BlackboardAsset = BlackboardData.Get();
		UBTComposite_Selector* Root = NewObject<UBTComposite_Selector>(ChaseTree.Get());
		ChaseTree->RootNode = Root;
		UBTComposite_Sequence* Chase = NewObject<UBTComposite_Sequence>(ChaseTree.Get());
		UBTTask_ActivateAbilityAndWait* Pursuit = NewObject<UBTTask_ActivateAbilityAndWait>(ChaseTree.Get());
		UBTDecorator_BossCanPursue* CanPursue = NewObject<UBTDecorator_BossCanPursue>(ChaseTree.Get());
		UBTTask_MoveTo* Move = NewObject<UBTTask_MoveTo>(ChaseTree.Get());
		Move->AcceptableRadius = 180.f;
		// Configure the same editable node properties used in the saved tree.
		// The real BT waits for the ability and can preempt the fallback Move To.
		auto SetNodeProperty = [this](UObject* Node, const TCHAR* Name, const FString& Value)
		{
			FProperty* Property = FindFProperty<FProperty>(Node->GetClass(), Name);
			if (!Test.TestNotNull(FString::Printf(TEXT("BT node exposes %s"), Name), Property)) return false;
			return Test.TestTrue(FString::Printf(TEXT("BT node accepts configuration for %s"), Name),
				Property->ImportText_Direct(*Value, Property->ContainerPtrToValuePtr<void>(Node), Node, PPF_None) != nullptr);
		};
		if (!SetNodeProperty(Move, TEXT("BlackboardKey"), TEXT("(SelectedKeyName=\"TargetActor\")")) ||
			!SetNodeProperty(Pursuit, TEXT("AbilityTag"), TEXT("(TagName=\"Boss.Ability.Attack.Pursuit\")"))) return false;
		Chase->Children.AddDefaulted_GetRef().ChildTask = Pursuit;
		FBTCompositeChild& ChaseChild = Root->Children.AddDefaulted_GetRef();
		ChaseChild.ChildComposite = Chase;
		ChaseChild.Decorators.Add(CanPursue);
		Root->Children.AddDefaulted_GetRef().ChildTask = Move;
		UBTTask_Wait* BusyWait = NewObject<UBTTask_Wait>(ChaseTree.Get());
		BusyWait->WaitTime = 10.f;
		Root->Children.AddDefaulted_GetRef().ChildTask = BusyWait;
		return Test.TestTrue(TEXT("A real chase behavior tree starts"), Controller->RunBehaviorTree(ChaseTree.Get()));
	}
	UBehaviorTreeComponent* TreeComponent() const
	{
		const AAIController* Controller = Cast<AAIController>(Boss->GetController());
		return Controller ? Cast<UBehaviorTreeComponent>(Controller->GetBrainComponent()) : nullptr;
	}
	bool TreeIsWaitingForAbility() const
	{
		const UBehaviorTreeComponent* Tree = TreeComponent();
		return Tree && Cast<UBTTask_ActivateAbilityAndWait>(Tree->GetActiveNode()) != nullptr;
	}
	bool TreeIsMoving() const
	{
		const UBehaviorTreeComponent* Tree = TreeComponent();
		return Tree && Cast<UBTTask_MoveTo>(Tree->GetActiveNode()) != nullptr;
	}
	void StopTree() { if (UBehaviorTreeComponent* Tree = TreeComponent()) Tree->StopTree(EBTStopMode::Forced); }
	void ClearTarget() { Blackboard->ClearValue(TEXT("TargetActor")); }
	void DestroyTarget() { Target->Destroy(); }
	void ReplaceTarget()
	{
		AActor* Replacement = World->SpawnActor<AActor>(AActor::StaticClass(),
			FTransform(Target->GetActorLocation() + FVector(100.f, 0.f, 0.f)));
		Blackboard->SetValueAsObject(TEXT("TargetActor"), Replacement);
	}
	void RestoreTarget() { Blackboard->SetValueAsObject(TEXT("TargetActor"), Target); }
	void PlaceTarget(const FVector& Offset)
	{
		Target->SetActorLocation(Boss->GetActorLocation() + Offset, false, nullptr, ETeleportType::TeleportPhysics);
	}
	bool PlaceTargetBeyondDashEnd(float Distance, float YawOffset = 0.f)
	{
		const FMotionWarpingTarget* Destination = Boss->GetMotionWarpingComponent()->FindWarpTarget(TEXT("BossPursuitTarget"));
		if (!Test.TestNotNull(TEXT("The running dodge has a real fixed warp destination"), Destination)) return false;
		const FVector Direction = FRotator(0.f, Boss->GetActorRotation().Yaw + YawOffset, 0.f).Vector();
		FVector Location = Destination->GetLocation() + Direction * Distance;
		Location.Z = Target->GetActorLocation().Z;
		Target->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		return true;
	}
	const FMotionWarpingTarget* AttackWarpTarget() const
	{
		return Boss->GetMotionWarpingComponent()->FindWarpTarget(TEXT("AttackTarget"));
	}
	bool GetSavedAttackWarpWindow(float& OutStart, float& OutEnd) const
	{
		const UAnimMontage* Montage = Boss->PursuitSettings.FollowupAttackMontage;
		if (!Montage) return false;
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			const UAnimNotifyState_MotionWarping* Notify = Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass);
			const URootMotionModifier_Warp* Modifier = Notify ? Cast<URootMotionModifier_Warp>(Notify->RootMotionModifier) : nullptr;
			if (Modifier && Modifier->WarpTargetName == TEXT("AttackTarget"))
			{
				OutStart = Event.GetTriggerTime();
				OutEnd = Event.GetEndTriggerTime();
				Test.AddInfo(FString::Printf(TEXT("Saved stinger AttackTarget notify: bWarpRotation=%d RotationType=%s"),
					Modifier->bWarpRotation, *StaticEnum<EMotionWarpRotationType>()->GetNameStringByValue(static_cast<int64>(Modifier->RotationType))));
				return Test.TestTrue(TEXT("The saved stinger warps translation on the horizontal plane"),
					Modifier->bWarpTranslation && Modifier->bIgnoreZAxis && OutStart > 0.01f && OutEnd > OutStart);
			}
		}
		return Test.TestTrue(TEXT("The saved stinger provides an AttackTarget motion-warping window"), false);
	}
	bool TickUntilAttackPosition(float Position)
	{
		UAnimMontage* Montage = Boss->PursuitSettings.FollowupAttackMontage;
		for (int32 Step = 0; Step < 600 && IsActive() && Anim->Montage_GetPosition(Montage) < Position; ++Step)
			TickFor(0.005f);
		return IsActive() && Anim->Montage_GetPosition(Montage) >= Position;
	}
	bool StartOrdinary(bool bThree) { return ASC->TryActivateAbility(bThree ? ThreeHandle : NormalHandle); }
	bool OrdinaryIsActive(bool bThree) const { return AbilityIsActive(bThree ? ThreeHandle : NormalHandle); }
	void CancelOrdinary(bool bThree) { ASC->CancelAbilityHandle(bThree ? ThreeHandle : NormalHandle); }
	bool StartUnrelated()
	{
		UnrelatedHandle = ASC->GiveAbility(FGameplayAbilitySpec(UFirstGameplayAbility::StaticClass(), 1));
		return ASC->TryActivateAbility(UnrelatedHandle);
	}
	bool UnrelatedIsActive() const
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(UnrelatedHandle);
		return Spec && Spec->IsActive();
	}
	void CancelUnrelated() { ASC->CancelAbilityHandle(UnrelatedHandle); }
	bool InitializeRetaliation(bool bWithReactiveAbilities = false)
	{
		if (!Test.TestNotNull(TEXT("The BOSS owns its production retaliation component"), Boss->RetaliationComponent.Get())) return false;
		Boss->RetaliationComponent->UpdateCombatContext(Target, true);
		if (bWithReactiveAbilities)
		{
			const UClass* BossClass = LoadClass<ABossCharacter>(nullptr, TEXT("/Game/Enemy/BP_Boss.BP_Boss_C"));
			const ABossCharacter* Defaults = BossClass ? BossClass->GetDefaultObject<ABossCharacter>() : nullptr;
			Boss->HitReactMontage = Defaults ? Defaults->GetHitReactMontage() : nullptr;
			if (!Test.TestNotNull(TEXT("The saved ordinary hit-react montage loads"), Boss->HitReactMontage.Get())) return false;
			WaitForMontageCompression(Boss->HitReactMontage);
			HitReactHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGA_Boss_HitReact::StaticClass(), 1));
			ParryStaggerHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGA_Boss_ParryStagger::StaticClass(), 1));
			ExecutableHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGA_Boss_Executable::StaticClass(), 1));
		}
		return true;
	}
	void DamageBoss(float Damage = 1.f, bool bAttackSource = true)
	{
		// Exercise the same damage execution, health callbacks and source/state
		// snapshot as a real sword hit, rather than manufacturing a damage event.
		FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
		Context.AddInstigator(Target, Target);
		if (bAttackSource) Context.SetAbility(GetDefault<UFirstGA_DKLightAttack>());
		FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(UFirstGE_Damage::StaticClass(), 1.f, Context);
		Spec.Data->SetSetByCallerMagnitude(MyGameplayTags::DK_SetByCaller_BaseDamage, Damage);
		TargetASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, ASC);
	}
	void DamageBossRepeatedly(int32 Count) { for (int32 Index = 0; Index < Count; ++Index) DamageBoss(); }
	bool HitReactIsActive() const { return AbilityIsActive(HitReactHandle); }
	bool ParryStaggerIsActive() const { return AbilityIsActive(ParryStaggerHandle); }
	bool ExecutableIsActive() const { return AbilityIsActive(ExecutableHandle); }
	void StartParryStagger()
	{
		FGameplayEventData Event;
		Event.EventTag = MyGameplayTags::Boss_Event_Parried;
		Event.Instigator = Target;
		Event.Target = Boss;
		Event.EventMagnitude = 1.f;
		ASC->HandleGameplayEvent(Event.EventTag, &Event);
	}
	bool RunRetaliationTree(float BranchCooldown = 20.f, float FallbackWaitTime = 60.f,
		bool bMirrorAttackBusy = false, bool bLegacyCooldown = false,
		bool bPostAttackRecovery = false, float RecoveryMin = 3.f, float RecoveryMax = 6.f)
	{
		AAIController* Controller = CastChecked<AAIController>(Boss->GetController());
		ChaseTree.Reset(NewObject<UBehaviorTree>(Controller));
		ChaseTree->BlackboardAsset = BlackboardData.Get();
		UBTComposite_Selector* Root = NewObject<UBTComposite_Selector>(ChaseTree.Get());
		ChaseTree->RootNode = Root;
		UBTComposite_Sequence* Attack = NewObject<UBTComposite_Sequence>(ChaseTree.Get());
		UBTDecorator_Cooldown* Cooldown = bLegacyCooldown
			? NewObject<UBTDecorator_Cooldown>(ChaseTree.Get())
			: NewObject<UBTDecorator_BossAttackCooldown>(ChaseTree.Get());
		UBTDecorator_BossAttackCooldown* RecoveryTemplate = Cast<UBTDecorator_BossAttackCooldown>(Cooldown);
		if (RecoveryTemplate)
		{
			RecoveryTemplate->bUsePostAttackRecovery = bPostAttackRecovery;
			RecoveryTemplate->RecoveryTimeMin = RecoveryMin;
			RecoveryTemplate->RecoveryTimeMax = RecoveryMax;
		}
		// Match the serialized original decorator: PostLoad disables its tick
		// when Observer Aborts is None. The production replacement stays live.
		if (bLegacyCooldown) Cooldown->PostLoad();
		// Existing request tests use a long cooldown to prove early entry;
		// recovery tests use the production interval without changing the GA.
		Cooldown->CoolDownTime = BranchCooldown;
		Attack->Children.AddDefaulted_GetRef().ChildTask = NewObject<UBTTask_BossPrepareRetaliation>(ChaseTree.Get());
		UBTTask_BossSelectAttack* Select = NewObject<UBTTask_BossSelectAttack>(ChaseTree.Get());
		FBossAttackOption& ExcludedOption = Select->AttackOptions.AddDefaulted_GetRef();
		ExcludedOption.AbilityTag = MyGameplayTags::Boss_Ability_Attack_ThreeCombo;
		ExcludedOption.MaxRange = 10.f;
		FBossAttackOption& NormalOption = Select->AttackOptions.AddDefaulted_GetRef();
		NormalOption.AbilityTag = MyGameplayTags::Boss_Ability_Attack_Normal;
		NormalOption.CooldownTag = MyGameplayTags::Boss_Cooldown_Attack_Normal;
		NormalOption.MaxRange = 400.f;
		Attack->Children.AddDefaulted_GetRef().ChildTask = Select;
		Attack->Children.AddDefaulted_GetRef().ChildTask = NewObject<UBTTask_BossActivateAbilityByTag>(ChaseTree.Get());
		FBTCompositeChild& AttackChild = Root->Children.AddDefaulted_GetRef();
		AttackChild.ChildComposite = Attack;
		AttackChild.Decorators.Add(Cooldown);
		UBTTask_Wait* Idle = NewObject<UBTTask_Wait>(ChaseTree.Get());
		Idle->WaitTime = FallbackWaitTime;
		FBTCompositeChild& RecoveryChild = Root->Children.AddDefaulted_GetRef();
		RecoveryChild.ChildTask = Idle;
		bMirrorAttackBusyToBlackboard = bMirrorAttackBusy;
		if (bMirrorAttackBusy)
		{
			UBTDecorator_Blackboard* NotBusy = NewObject<UBTDecorator_Blackboard>(ChaseTree.Get());
			auto SetConditionProperty = [this, NotBusy](const TCHAR* Name, const TCHAR* Value)
			{
				FProperty* Property = FindFProperty<FProperty>(NotBusy->GetClass(), Name);
				return Test.TestNotNull(FString::Printf(TEXT("The real blackboard decorator exposes %s"), Name), Property) &&
					Test.TestTrue(FString::Printf(TEXT("The recovery condition accepts %s"), Name),
						Property->ImportText_Direct(Value, Property->ContainerPtrToValuePtr<void>(NotBusy), NotBusy, PPF_None) != nullptr);
			};
			if (!SetConditionProperty(TEXT("BlackboardKey"), TEXT("(SelectedKeyName=\"bIsBusy\")")) ||
				!SetConditionProperty(TEXT("OperationType"), TEXT("1")) ||
				!SetConditionProperty(TEXT("BasicOperation"), TEXT("NotSet")) ||
				!SetConditionProperty(TEXT("FlowAbortMode"), TEXT("Both"))) return false;
			RecoveryChild.Decorators.Add(NotBusy);
			Blackboard->SetValueAsBool(TEXT("bIsBusy"), ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Attacking));
		}
		return Test.TestTrue(TEXT("The real selection-and-activation retaliation tree starts"), Controller->RunBehaviorTree(ChaseTree.Get()));
	}
	UBTDecorator_BossAttackCooldown* RecoveryNode() const
	{
		UBehaviorTreeComponent* Tree = TreeComponent();
		if (!Tree) return nullptr;
		UBTDecorator_BossAttackCooldown* Result = nullptr;
		// BehaviorTreeManager duplicates asset nodes before assigning offsets;
		// enumerate the live instances even when Busy leaves no task active.
		Tree->ForEachChildTask([Tree, &Result](UBTTaskNode&, const FBehaviorTreeInstance& Instance, int32 InstanceIndex)
		{
			if (Result) return;
			for (const FBTCompositeChild& Child : Instance.RootNode->Children)
			{
				for (UBTDecorator* Decorator : Child.Decorators)
				{
					if (UBTDecorator_BossAttackCooldown* Template = Cast<UBTDecorator_BossAttackCooldown>(Decorator))
					{
						Result = Cast<UBTDecorator_BossAttackCooldown>(
							Template->GetNodeInstance(*Tree, Tree->GetNodeMemory(Template, InstanceIndex)));
						return;
					}
				}
			}
		});
		return Result;
	}
	bool RecoveryAllowsEntry() const
	{
		UBehaviorTreeComponent* Tree = TreeComponent();
		UBTDecorator_BossAttackCooldown* Runtime = RecoveryNode();
		if (!Tree || !Runtime) return false;
		const int32 InstanceIndex = Tree->FindInstanceContainingNode(Runtime->GetParentNode());
		return InstanceIndex != INDEX_NONE && Runtime->CalculateRawConditionValue(*Tree, Tree->GetNodeMemory(Runtime, InstanceIndex));
	}
	bool WaitForOrdinaryEnd(bool bThree)
	{
		for (int32 Step = 0; Step < 2400 && OrdinaryIsActive(bThree); ++Step) TickFor(0.005f);
		return !OrdinaryIsActive(bThree);
	}
	bool TreeIsWaiting() const
	{
		const UBehaviorTreeComponent* Tree = TreeComponent();
		return Tree && Cast<UBTTask_Wait>(Tree->GetActiveNode()) != nullptr;
	}
	FName SelectedAttackTag() const { return Blackboard->GetValueAsName(TEXT("AttackTag")); }
	void ClearAttackSelection() { Blackboard->ClearValue(TEXT("AttackTag")); }
	float TargetHealth() const { return TargetASC->GetNumericAttribute(UFirstAttributeSet::GetHealthAttribute()); }
	void HitTarget()
	{
		FGameplayEventData Event;
		Event.EventTag = MyGameplayTags::DK_Event_MeleeHit;
		Event.Instigator = Boss;
		Event.Target = Target;
		ASC->HandleGameplayEvent(Event.EventTag, &Event);
	}
	void EnableTargetParry()
	{
		Target->SetActorRotation((Boss->GetActorLocation() - Target->GetActorLocation()).Rotation());
		TargetASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_ParryWindow);
	}
	bool BeginDodge()
	{
		DodgeStartLocation = Boss->GetActorLocation();
		if (!Test.TestTrue(TEXT("The independent chase ability activates"), TryStart())) return false;
		ObserveMontage();
		for (int32 Step = 0; Step < 200 && DodgeCount() == 0 && IsActive(); ++Step) TickFor(0.005f);
		if (!Test.TestEqual(TEXT("A real forward-dodge montage starts"), DodgeCount(), 1)) return false;
		CommittedYaw = Boss->GetActorRotation().Yaw;
		return true;
	}
	void MoveTarget(const FVector& Direction, float Speed)
	{
		TargetDirection = Direction.GetSafeNormal2D();
		Target->GetCharacterMovement()->MaxWalkSpeed = Speed;
	}
	void Cancel() { ASC->CancelAbilityHandle(PursuitHandle); }
	void InterruptMontage() { Anim->Montage_Stop(0.02f); }
	void BlockRemainingRoute()
	{
		AActor* Wall = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->AddInstanceComponent(Box);
		Wall->SetRootComponent(Box);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetBoxExtent(FVector(20.f, 300.f, 150.f));
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetWorldLocation(Boss->GetActorLocation() + Boss->GetActorForwardVector() * 100.f);
		Box->RegisterComponent();
	}
	void Parry()
	{
		FGameplayEventData Event;
		Event.EventTag = MyGameplayTags::Boss_Event_Parried;
		Event.Instigator = Target;
		Event.Target = Boss;
		ASC->HandleGameplayEvent(Event.EventTag, &Event);
	}
	void MarkTargetDead() { TargetASC->AddLooseGameplayTag(MyGameplayTags::Shared_Status_Dead); }
	bool WaitForFollowup()
	{
		for (int32 Step = 0; Step < 500 && StartedCount() < 2 && IsActive(); ++Step)
		{
			TickFor(0.005f);
			if (Step % 20 == 0)
			{
				Test.AddInfo(FString::Printf(TEXT("Dash sample: t=%.3f montage=%.3f location=%s velocity=%s root=%d weight=%.3f"),
					Step * 0.005f, Anim->Montage_GetPosition(DodgeMontage), *Boss->GetActorLocation().ToCompactString(),
					*Boss->GetVelocity().ToCompactString(), Boss->IsPlayingRootMotion(),
					Anim->GetRootMotionMontageInstance() ? Anim->GetRootMotionMontageInstance()->GetWeight() : 0.f));
			}
		}
		return StartedCount() == 2;
	}
	bool WaitForAbilityEnd()
	{
		for (int32 Step = 0; Step < 1600 && IsActive(); ++Step) TickFor(0.005f);
		return !IsActive();
	}
	float ActualDashTravel() const { return FVector::Dist2D(DodgeStartLocation, Boss->GetActorLocation()); }
	float FacingCorrection() const { return FMath::Abs(FMath::FindDeltaAngleDegrees(CommittedYaw, Boss->GetActorRotation().Yaw)); }
	void TickFor(float Duration)
	{
		const int32 Steps = FMath::CeilToInt(Duration / 0.005f);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			if (bMirrorAttackBusyToBlackboard)
				Blackboard->SetValueAsBool(TEXT("bIsBusy"), ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Attacking));
			if (Target && !TargetDirection.IsNearlyZero()) Target->AddMovementInput(TargetDirection, 1.f, true);
			World->Tick(LEVELTICK_All, Duration / Steps);
			ObserveMontage();
			++GFrameCounter;
		}
	}

private:
	bool AbilityIsActive(FGameplayAbilitySpecHandle Handle) const
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
		return Spec && Spec->IsActive();
	}
	static void WaitForMontageCompression(UAnimMontage* Montage)
	{
		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
			{
				if (UAnimSequence* Sequence = Cast<UAnimSequence>(Segment.GetAnimReference())) Sequence->WaitOnExistingCompression();
			}
		}
	}
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
	void AddFloor()
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->AddInstanceComponent(Box);
		Actor->SetRootComponent(Box);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetBoxExtent(FVector(2000.f, 2000.f, 50.f));
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetWorldLocation(FVector(0.f, 0.f, -50.f));
		Box->RegisterComponent();
	}
	void ObserveMontage()
	{
		UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		const FAnimMontageInstance* Instance = Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
		if (Instance && !StartedIDs.Contains(Instance->GetInstanceID()))
		{
			StartedIDs.Add(Instance->GetInstanceID());
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
	UAnimMontage* DodgeMontage = nullptr;
	UBlackboardComponent* Blackboard = nullptr;
	TStrongObjectPtr<UBlackboardData> BlackboardData;
	TStrongObjectPtr<UBehaviorTree> ChaseTree;
	TStrongObjectPtr<UBehaviorTreeComponent> PolicyComponent;
	TStrongObjectPtr<UBehaviorTree> PolicyTree;
	TStrongObjectPtr<UBTDecorator_BossCanPursue> PolicyCondition;
	FGameplayAbilitySpecHandle NormalHandle;
	FGameplayAbilitySpecHandle ThreeHandle;
	FGameplayAbilitySpecHandle PursuitHandle;
	FGameplayAbilitySpecHandle UnrelatedHandle;
	FGameplayAbilitySpecHandle HitReactHandle;
	FGameplayAbilitySpecHandle ParryStaggerHandle;
	FGameplayAbilitySpecHandle ExecutableHandle;
	FDelegateHandle AttackingTagHandle;
	TSet<int32> StartedIDs;
	TArray<UAnimMontage*> StartedMontages;
	FVector TargetDirection = FVector::ZeroVector;
	FVector DodgeStartLocation = FVector::ZeroVector;
	float CommittedYaw = 0.f;
	int32 AttackingTagDrops = 0;
	bool bReady = false;
	bool bMirrorAttackBusyToBlackboard = false;
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossDashMovingTargetTest,
	"First.Combat.BossPursuit.RealDashCommitsAttackAgainstMovingTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossDashMovingTargetTest::RunTest(const FString& Parameters)
{
	// Ordinary retreat exceeds the direct-attack range. Fast retreat also
	// crosses the original pursuit-trigger range; sidestepping exceeds 35 deg.
	for (int32 Scenario = 0; Scenario < 4; ++Scenario)
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) return false;
		if (Scenario == 3 && !Fixture.UseSavedCustomFollowup()) return false;
		if (!Fixture.BeginDodge()) return false;
		Fixture.MoveTarget(Scenario == 2 ? FVector::RightVector : FVector::ForwardVector, Scenario == 1 ? 1800.f : 600.f);
		if (!TestTrue(FString::Printf(TEXT("Moving-target scenario %d completes the committed dodge into an attack"), Scenario),
			Fixture.WaitForFollowup())) continue;
		const ABossCharacter* Boss = Fixture.GetBoss();
		const FVector Delta = Fixture.GetTarget()->GetActorLocation() - Boss->GetActorLocation();
		TestTrue(TEXT("Actual character root motion moved the BOSS forward"), Fixture.ActualDashTravel() > 100.f);
		TestTrue(TEXT("The target moved outside direct attack selection range"), Delta.Size2D() > Boss->PursuitSettings.DirectAttackRange);
		if (Scenario == 1)
		{
			TestTrue(TEXT("Fast retreat crosses the behavior-tree trigger range after commitment"), Delta.Size2D() > 650.f);
		}
		if (Scenario == 2)
		{
			TestTrue(TEXT("The sidestep exceeds the allowed correction cone"),
				FMath::Abs(Delta.Rotation().Yaw) > Boss->PursuitSettings.MaxAttackFacingCorrection + 5.f);
		}
		TestTrue(TEXT("The followup's facing correction stays within the configured limit"),
			Fixture.FacingCorrection() <= Boss->PursuitSettings.MaxAttackFacingCorrection + 0.5f);
		TestTrue(TEXT("The independent chase remains active while the followup plays"), Fixture.IsActive());
		TestEqual(TEXT("Attacking remains continuous from dash to followup"), Fixture.BusyDrops(), 0);
		TestEqual(TEXT("The selected followup montage actually plays"), Fixture.LastMontage(),
			Boss->PursuitSettings.FollowupAttackMontage ? Boss->PursuitSettings.FollowupAttackMontage.Get() : Boss->GetNormalAttackMontage());
		Fixture.MoveTarget(FVector::ZeroVector, 0.f);
		Fixture.TickFor(Boss->GetNormalAttackMontage()->GetPlayLength() + 0.6f);
		TestEqual(TEXT("Only the dodge and one followup play"), Fixture.StartedCount(), 2);
		TestEqual(TEXT("The normal montage plays only when selected as the followup"), Fixture.NormalCount(), Scenario == 3 ? 0 : 1);
		TestEqual(TEXT("Only one forward dodge plays"), Fixture.DodgeCount(), 1);
		TestFalse(TEXT("The full round ends after that attack"), Fixture.IsActive());
		TestEqual(TEXT("Attacking drops only at the final end"), Fixture.BusyDrops(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossPursuitAttackWarpStrengthTest,
	"First.Combat.BossPursuit.IndependentAttackWarpChangesRealStingerTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossPursuitAttackWarpStrengthTest::RunTest(const FString& Parameters)
{
	float WindowTravel[2] = {};
	for (int32 Scenario = 0; Scenario < 2; ++Scenario)
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.UseSavedCustomFollowup()) return false;
		ABossCharacter* Boss = Fixture.GetBoss();
		FFirstBossPursuitSettings& Settings = Boss->PursuitSettings;
		Settings.DirectAttackRange = 250.f;
		Settings.AttackWarpRange = 450.f;
		Settings.AttackWarpMaxTravelDistance = Scenario == 0 ? 120.f : 300.f;
		Settings.AttackWarpSurfaceGap = 15.f;
		float WarpStart = 0.f, WarpEnd = 0.f;
		if (!Fixture.GetSavedAttackWarpWindow(WarpStart, WarpEnd) || !Fixture.BeginDodge()) return false;
		Fixture.TickFor(0.15f);
		// Move the player only after the dash is committed. Both runs have the
		// same real dodge and stinger; only the attack's travel cap differs.
		if (!Fixture.PlaceTargetBeyondDashEnd(380.f) ||
			!TestTrue(TEXT("The committed dodge reaches the saved stinger"), Fixture.WaitForFollowup())) return false;
		const FVector AttackStart = Boss->GetActorLocation();
		const float AttackDistance = FVector::Dist2D(AttackStart, Fixture.GetTarget()->GetActorLocation());
		TestTrue(TEXT("Attack acquisition can exceed the old dash-landing gate without changing that gate"),
			AttackDistance > Settings.DirectAttackRange && AttackDistance < Settings.AttackWarpRange);
		const FMotionWarpingTarget* Warp = Fixture.AttackWarpTarget();
		if (!TestNotNull(TEXT("The independent attack range creates its own warp target"), Warp)) return false;
		const FVector LockedDestination = Warp->GetLocation();
		const float PlannedTravel = FVector::Dist2D(AttackStart, LockedDestination);
		TestTrue(TEXT("The attack destination respects the independently configured travel cap"),
			PlannedTravel <= Settings.AttackWarpMaxTravelDistance + 1.f);
		if (Scenario == 0)
			TestTrue(TEXT("The short travel cap limits the attack destination to 120 cm"), FMath::IsNearlyEqual(PlannedTravel, 120.f, 1.f));
		else
		{
			const float SurfaceGap = FVector::Dist2D(LockedDestination, Fixture.GetTarget()->GetActorLocation()) -
				Boss->GetCapsuleComponent()->GetScaledCapsuleRadius() - Fixture.GetTarget()->GetCapsuleComponent()->GetScaledCapsuleRadius();
			TestTrue(TEXT("A sufficient travel cap stops the destination at the configured capsule surface gap"),
				FMath::IsNearlyEqual(SurfaceGap, Settings.AttackWarpSurfaceGap, 1.f));
		}
		if (!TestTrue(TEXT("The real attack reaches the frame before its saved warp window"),
			Fixture.TickUntilAttackPosition(WarpStart - 0.01f))) return false;
		const FVector BeforeWindow = Boss->GetActorLocation();
		if (!TestTrue(TEXT("The real attack plays through its saved warp window"),
			Fixture.TickUntilAttackPosition(WarpEnd + 0.035f))) return false;
		WindowTravel[Scenario] = FVector::DotProduct(Boss->GetActorLocation() - BeforeWindow, Boss->GetActorForwardVector());
		AddInfo(FString::Printf(TEXT("Saved stinger: cap=%.1f attack gap=%.1f planned=%.1f actual warp-window advance=%.1f"),
			Settings.AttackWarpMaxTravelDistance, AttackDistance, PlannedTravel, WindowTravel[Scenario]));
		TestTrue(TEXT("Motion warping produces real forward character movement"), WindowTravel[Scenario] > 5.f);
		Fixture.Cancel();
		TestNull(TEXT("Ending the pursuit removes its attack warp target"), Fixture.AttackWarpTarget());
	}
	TestTrue(TEXT("Raising only pursuit attack strength produces substantially more real stinger advance"),
		WindowTravel[1] > WindowTravel[0] + 70.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossPursuitAttackWarpRejectionTest,
	"First.Combat.BossPursuit.DisabledAttackWarpPreservesAnimationAndOrdinaryAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossPursuitAttackWarpRejectionTest::RunTest(const FString& Parameters)
{
	for (int32 Scenario = 0; Scenario < 3; ++Scenario)
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.UseSavedCustomFollowup()) return false;
		ABossCharacter* Boss = Fixture.GetBoss();
		FFirstBossPursuitSettings& Settings = Boss->PursuitSettings;
		Settings.AttackWarpRange = Scenario == 0 ? 0.f : (Scenario == 2 ? 250.f : 450.f);
		Settings.AttackWarpMaxTravelDistance = Scenario == 1 ? 0.f : 300.f;
		float WarpStart = 0.f, WarpEnd = 0.f;
		if (!Fixture.GetSavedAttackWarpWindow(WarpStart, WarpEnd) || !Fixture.BeginDodge()) return false;
		Fixture.TickFor(0.15f);
		if (!Fixture.PlaceTargetBeyondDashEnd(380.f)) return false;
		// Simulate a stale target left by another caller before the attack
		// phase. Disabled or out-of-range pursuit must clear it explicitly.
		Boss->GetMotionWarpingComponent()->AddOrUpdateWarpTargetFromLocation(TEXT("AttackTarget"),
			Boss->GetActorLocation() + FVector(900.f, 300.f, 0.f));
		if (!TestTrue(TEXT("Disabling attack attraction does not swallow the committed stinger"), Fixture.WaitForFollowup())) return false;
		TestNull(TEXT("Zero acquisition, zero strength, or excessive range removes stale AttackTarget"), Fixture.AttackWarpTarget());
		const FVector AttackStart = Boss->GetActorLocation();
		if (!TestTrue(TEXT("The unwarped stinger still passes its real animation window"),
			Fixture.TickUntilAttackPosition(WarpEnd + 0.035f))) return false;
		TestTrue(TEXT("Disabling attraction preserves the animation's native root displacement"),
			FVector::Dist2D(AttackStart, Boss->GetActorLocation()) > 10.f);
		TestNull(TEXT("The saved notify cannot resurrect a rejected attack target"), Fixture.AttackWarpTarget());
		Fixture.Cancel();
		if (Scenario == 0)
		{
			// The same actor can still use ordinary attack's own settings even
			// when all pursuit attraction controls have been disabled/changed.
			Settings.AttackWarpRange = 0.f;
			Settings.AttackWarpMaxTravelDistance = 0.f;
			Settings.AttackWarpSurfaceGap = 80.f;
			Fixture.PlaceTarget(Boss->GetActorForwardVector() * 290.f);
			const FVector OrdinaryStart = Boss->GetActorLocation();
			if (!TestTrue(TEXT("Ordinary attack remains usable after disabling pursuit attraction"), Fixture.StartOrdinary(false))) return false;
			const FMotionWarpingTarget* OrdinaryWarp = Fixture.AttackWarpTarget();
			if (!TestNotNull(TEXT("Ordinary attack creates a warp target independently of pursuit settings"), OrdinaryWarp)) return false;
			TestTrue(TEXT("Ordinary attack retains its existing 140 cm travel cap"),
				FMath::IsNearlyEqual(FVector::Dist2D(OrdinaryStart, OrdinaryWarp->GetLocation()), 140.f, 1.f));
			Fixture.CancelOrdinary(false);
			TestNull(TEXT("Ordinary attack cleanup still removes its own target"), Fixture.AttackWarpTarget());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossPursuitAttackAimOffsetTest,
	"First.Combat.BossPursuit.AttackAimOffsetSurvivesWarpAndRespectsFacingCone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossPursuitAttackAimOffsetTest::RunTest(const FString& Parameters)
{
	for (int32 Scenario = 0; Scenario < 3; ++Scenario)
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.UseSavedCustomFollowup()) return false;
		ABossCharacter* Boss = Fixture.GetBoss();
		FFirstBossPursuitSettings& Settings = Boss->PursuitSettings;
		Settings.AttackWarpRange = Scenario == 1 ? 0.f : 450.f;
		Settings.AttackWarpMaxTravelDistance = 300.f;
		Settings.AttackAimYawOffset = Scenario == 0 ? -10.f : -45.f;
		Settings.AttackWarpLateralOffset = 0.f;
		float WarpStart = 0.f, WarpEnd = 0.f;
		if (!Fixture.GetSavedAttackWarpWindow(WarpStart, WarpEnd) || !Fixture.BeginDodge()) return false;
		const float CommittedYaw = Boss->GetActorRotation().Yaw;
		Fixture.TickFor(0.15f);
		// The third case deliberately leaves the original target-facing cone.
		// A compensating left aim offset must not reopen translation attraction.
		if (!Fixture.PlaceTargetBeyondDashEnd(380.f, Scenario == 2 ? 55.f : 0.f) ||
			!TestTrue(TEXT("The committed dash reaches the stinger with an aim offset"), Fixture.WaitForFollowup())) return false;
		const float TargetYaw = (Fixture.GetTarget()->GetActorLocation() - Boss->GetActorLocation()).Rotation().Yaw;
		const float TargetDelta = FMath::FindDeltaAngleDegrees(CommittedYaw, TargetYaw);
		const float ActualDelta = FMath::FindDeltaAngleDegrees(CommittedYaw, Boss->GetActorRotation().Yaw);
		const float ExpectedDelta = FMath::Clamp(TargetDelta + Settings.AttackAimYawOffset,
			-Settings.MaxAttackFacingCorrection, Settings.MaxAttackFacingCorrection);
		TestTrue(TEXT("The real character applies the configured signed aim correction at attack start"),
			FMath::IsNearlyEqual(ActualDelta, ExpectedDelta, 1.f));
		TestTrue(TEXT("Aim compensation remains inside the existing total facing correction limit"),
			FMath::Abs(ActualDelta) <= Settings.MaxAttackFacingCorrection + 0.5f);
		const float LockedAttackYaw = Boss->GetActorRotation().Yaw;
		if (Scenario == 0)
		{
			TestTrue(TEXT("A negative offset turns the body left of the target line"),
				FMath::FindDeltaAngleDegrees(TargetYaw, LockedAttackYaw) < -8.f);
			const FMotionWarpingTarget* Warp = Fixture.AttackWarpTarget();
			if (!TestNotNull(TEXT("Aim compensation can coexist with translation attraction"), Warp)) return false;
			TestTrue(TEXT("The saved rotation warp target retains the compensated body yaw"),
				FMath::Abs(FMath::FindDeltaAngleDegrees(LockedAttackYaw, Warp->GetRotation().Rotator().Yaw)) < 0.5f);
		}
		else
		{
			TestNull(TEXT("Disabled attraction and out-of-cone targets do not publish an attack warp target"), Fixture.AttackWarpTarget());
			if (Scenario == 1)
				TestTrue(TEXT("Yaw compensation still applies when translation attraction is disabled"), ActualDelta < -34.f);
			else
				TestTrue(TEXT("The moved target really lies outside the original facing cone"),
					FMath::Abs(TargetDelta) > Settings.MaxAttackFacingCorrection + 10.f);
		}
		if (!TestTrue(TEXT("The compensated stinger reaches the end of its actual saved warp window"),
			Fixture.TickUntilAttackPosition(WarpEnd + 0.035f))) return false;
		if (Scenario == 0)
			TestTrue(TEXT("The animation's rotation-warp notify does not turn the compensated body back toward the player"),
				FMath::Abs(FMath::FindDeltaAngleDegrees(LockedAttackYaw, Boss->GetActorRotation().Yaw)) < 2.f);
		else
			TestNull(TEXT("The real notify cannot resurrect translation attraction after rejection"), Fixture.AttackWarpTarget());
		Fixture.Cancel();
		TestNull(TEXT("Cancellation clears the compensated attack target"), Fixture.AttackWarpTarget());
		TestFalse(TEXT("Cancellation releases the pursuit direction lock"), Fixture.HasTag(MyGameplayTags::Boss_Status_AttackDirectionLocked));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossPursuitAttackLateralOffsetTest,
	"First.Combat.BossPursuit.AttackLateralOffsetChangesRealStingerTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossPursuitAttackLateralOffsetTest::RunTest(const FString& Parameters)
{
	for (const float LateralOffset : { -40.f, 40.f })
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.UseSavedCustomFollowup()) return false;
		ABossCharacter* Boss = Fixture.GetBoss();
		FFirstBossPursuitSettings& Settings = Boss->PursuitSettings;
		Settings.AttackWarpRange = 450.f;
		Settings.AttackWarpMaxTravelDistance = 180.f;
		Settings.AttackWarpSurfaceGap = 15.f;
		Settings.AttackAimYawOffset = 0.f;
		Settings.AttackWarpLateralOffset = LateralOffset;
		// Rotate the whole encounter so world Y cannot accidentally stand in
		// for the attack's local right axis.
		Boss->SetActorRotation(FRotator(0.f, 67.f, 0.f));
		Fixture.PlaceTarget(Boss->GetActorForwardVector() * 550.f);
		float WarpStart = 0.f, WarpEnd = 0.f;
		if (!Fixture.GetSavedAttackWarpWindow(WarpStart, WarpEnd) || !Fixture.BeginDodge()) return false;
		Fixture.TickFor(0.15f);
		if (!Fixture.PlaceTargetBeyondDashEnd(380.f) ||
			!TestTrue(TEXT("The dash reaches the stinger with a signed lateral offset"), Fixture.WaitForFollowup())) return false;
		const FVector AttackStart = Boss->GetActorLocation();
		const FVector AimDirection = (Fixture.GetTarget()->GetActorLocation() - AttackStart).GetSafeNormal2D();
		const FVector RightDirection = FVector::CrossProduct(FVector::UpVector, AimDirection);
		const FMotionWarpingTarget* Warp = Fixture.AttackWarpTarget();
		if (!TestNotNull(TEXT("The lateral-offset attack publishes a fixed warp target"), Warp)) return false;
		const FVector LockedDestination = Warp->GetLocation();
		const FVector PlannedDelta = LockedDestination - AttackStart;
		const float SignedPlannedLateral = FVector::DotProduct(PlannedDelta, RightDirection);
		TestTrue(TEXT("The destination moves to the requested side of the target line"),
			SignedPlannedLateral * FMath::Sign(LateralOffset) > 10.f);
		TestTrue(TEXT("The combined forward and lateral destination stays within the configured travel cap"),
			FMath::IsNearlyEqual(PlannedDelta.Size2D(), Settings.AttackWarpMaxTravelDistance, 1.f));
		TestTrue(TEXT("The lateral destination stays on the attack's horizontal plane"),
			FMath::IsNearlyEqual(LockedDestination.Z, AttackStart.Z, 0.5f));
		if (!TestTrue(TEXT("The lateral attack reaches the frame before its real warp window"),
			Fixture.TickUntilAttackPosition(WarpStart - 0.01f))) return false;
		const FVector BeforeWindow = Boss->GetActorLocation();
		if (!TestTrue(TEXT("The lateral attack plays through its real warp window"),
			Fixture.TickUntilAttackPosition(WarpEnd + 0.035f))) return false;
		const float SignedActualLateral = FVector::DotProduct(Boss->GetActorLocation() - BeforeWindow, RightDirection);
		AddInfo(FString::Printf(TEXT("Stinger lateral offset=%.1f planned=%.1f actual warp-window side movement=%.1f"),
			LateralOffset, SignedPlannedLateral, SignedActualLateral));
		TestTrue(TEXT("Real character root motion moves toward the configured side"),
			SignedActualLateral * FMath::Sign(LateralOffset) > 5.f);
		Warp = Fixture.AttackWarpTarget();
		if (!TestNotNull(TEXT("The attack keeps its fixed target through the warp notify"), Warp)) return false;
		TestTrue(TEXT("The notify preserves the selected lateral endpoint"), Warp->GetLocation().Equals(LockedDestination, 0.1f));
		TestTrue(TEXT("The actual offset attack finishes normally"), Fixture.WaitForAbilityEnd());
		TestNull(TEXT("Natural completion removes the lateral attack warp target"), Fixture.AttackWarpTarget());
		TestFalse(TEXT("Natural completion releases the direction lock"), Fixture.HasTag(MyGameplayTags::Boss_Status_AttackDirectionLocked));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossDashCancellationTest,
	"First.Combat.BossPursuit.RealDashCancellationNeverResurrectsAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossDashCancellationTest::RunTest(const FString& Parameters)
{
	for (int32 Scenario = 0; Scenario < 7; ++Scenario)
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.BeginDodge()) return false;
		Fixture.TickFor(0.15f);
		AddInfo(FString::Printf(TEXT("Dash distance before cancel: %.3f"), Fixture.ActualDashTravel()));
		TestTrue(TEXT("Cancellation interrupts a physically moving dash"), Fixture.ActualDashTravel() > 1.f);
		if (Scenario == 0) Fixture.Cancel();
		else if (Scenario == 1) Fixture.Parry();
		else if (Scenario == 2) Fixture.MarkTargetDead();
		else if (Scenario == 3) Fixture.DestroyTarget();
		else if (Scenario == 4) Fixture.SetBossTag(MyGameplayTags::Boss_Status_Staggered, true);
		else if (Scenario == 5) Fixture.InterruptMontage();
		else Fixture.BlockRemainingRoute();
		// The target-death route is polled by the production safety timer.
		Fixture.TickFor(0.03f);
		TestFalse(TEXT("Cancellation condition ends the actual dash ability"), Fixture.IsActive());
		const FVector StoppedAt = Fixture.GetBoss()->GetActorLocation();
		Fixture.TickFor(2.f);
		TestEqual(TEXT("A cancelled dash never starts its followup attack"), Fixture.NormalCount(), 0);
		TestEqual(TEXT("No replacement dash starts after cancellation"), Fixture.DodgeCount(), 1);
		TestTrue(TEXT("The cancelled root-motion dash remains stopped"),
			FVector::Dist2D(StoppedAt, Fixture.GetBoss()->GetActorLocation()) < 2.f);
		TestEqual(TEXT("Cancellation releases the attacking state once"), Fixture.BusyDrops(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossChaseEligibilityTest,
	"First.Combat.BossPursuit.ChaseEligibilityAndDistanceBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossChaseEligibilityTest::RunTest(const FString& Parameters)
{
	FirstBossDashFollowupTests::FFixture Fixture(*this);
	if (!Fixture.IsReady()) return false;
	FFirstBossPursuitSettings& Settings = Fixture.GetBoss()->PursuitSettings;
	const UBTDecorator_BossCanPursue* ConditionDefaults = GetDefault<UBTDecorator_BossCanPursue>();
	TestEqual(TEXT("The BT policy's initial minimum chase distance is 500 cm"), ConditionDefaults->MinTriggerRange, 500.f);
	TestEqual(TEXT("The BT policy's initial maximum chase distance is 650 cm"), ConditionDefaults->MaxTriggerRange, 650.f);
	TestEqual(TEXT("The initial independent chase cooldown is four seconds"), Settings.Cooldown, 4.f);
	for (float Distance : { 499.f, 500.f, 650.f, 651.f })
	{
		Fixture.PlaceTarget(FVector(Distance, 0.f, 0.f));
		TestEqual(FString::Printf(TEXT("Chase eligibility at %.0f cm uses the inclusive configured interval"), Distance),
			Fixture.CanChase(), Distance >= 500.f && Distance <= 650.f);
	}
	Fixture.PlaceTarget(FVector(550.f, 0.f, 0.f));
	for (FName RequiredKey : { FName(TEXT("bShouldChase")), FName(TEXT("bPlayerTooFar")) })
	{
		Fixture.SetBlackboardBool(RequiredKey, false);
		TestFalse(TEXT("A target in range alone cannot select pursuit outside distant chase"), Fixture.CanChase());
		TestTrue(TEXT("The skill's physical eligibility does not depend on chase blackboard flags"), Fixture.CanActivate());
		Fixture.SetBlackboardBool(RequiredKey, true);
	}
	for (FName BlockingKey : { FName(TEXT("bIsBusy")), FName(TEXT("bProvokedByDamage")) })
	{
		Fixture.SetBlackboardBool(BlockingKey, true);
		TestFalse(TEXT("Busy and damage-alert states prevent behavior-tree chase selection"), Fixture.CanChase());
		TestTrue(TEXT("Behavior policy blackboard flags are not read by the skill"), Fixture.CanActivate());
		Fixture.SetBlackboardBool(BlockingKey, false);
	}
	Settings.bEnabled = false;
	TestFalse(TEXT("The pursuit switch prevents activation"), Fixture.TryStart());
	Settings.bEnabled = true;
	Fixture.ClearTarget();
	TestFalse(TEXT("A missing target cannot start chase"), Fixture.TryStart());
	Fixture.RestoreTarget();
	Fixture.SetBossTag(MyGameplayTags::Boss_Status_WeaponDrawn, false);
	TestFalse(TEXT("The sword must be drawn before chase"), Fixture.TryStart());
	Fixture.SetBossTag(MyGameplayTags::Boss_Status_WeaponDrawn, true);
	const FGameplayTag BlockingTags[] = { MyGameplayTags::Boss_Status_Staggered,
		MyGameplayTags::Boss_Status_Attacking, MyGameplayTags::Shared_Status_Dead,
		MyGameplayTags::Boss_Status_Executable, MyGameplayTags::Boss_Status_BeingExecuted };
	for (FGameplayTag BlockingTag : BlockingTags)
	{
		Fixture.SetBossTag(BlockingTag, true);
		TestFalse(TEXT("A conflicting combat status prevents chase activation"), Fixture.TryStart());
		Fixture.SetBossTag(BlockingTag, false);
	}
	Fixture.PlaceTarget(FVector(-550.f, 0.f, 0.f));
	TestFalse(TEXT("A target behind the initial facing cone cannot start chase"), Fixture.TryStart());
	Fixture.PlaceTarget(FVector(550.f, 0.f, Settings.MaxTargetHeightDifference + 1.f));
	TestFalse(TEXT("A target above the permitted height cannot start chase"), Fixture.TryStart());
	Fixture.PlaceTarget(FVector(550.f, 0.f, 0.f));
	UAnimMontage* SavedDodge = Settings.ForwardDodgeMontage;
	Settings.ForwardDodgeMontage = nullptr;
	TestFalse(TEXT("Missing both dodge animation options prevents chase"), Fixture.TryStart());
	Settings.ForwardDodgeMontage = SavedDodge;
	TestFalse(TEXT("Rejected starts consume no chase cooldown"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_Pursuit));
	TestFalse(TEXT("Rejected starts leave no direction lock"), Fixture.HasTag(MyGameplayTags::Boss_Status_AttackDirectionLocked));
	TestEqual(TEXT("Rejected starts play no montage"), Fixture.StartedCount(), 0);
	TestTrue(TEXT("Restoring all prerequisites restores activation eligibility"), Fixture.CanActivate());
	TestTrue(TEXT("Restoring all prerequisites restores behavior-tree eligibility"), Fixture.CanChase());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossChaseIndependentCooldownTest,
	"First.Combat.BossPursuit.IndependentChaseCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossChaseIndependentCooldownTest::RunTest(const FString& Parameters)
{
	FirstBossDashFollowupTests::FFixture Fixture(*this);
	if (!Fixture.IsReady()) return false;
	Fixture.GetBoss()->PursuitSettings.Cooldown = 0.25f;
	if (!Fixture.BeginDodge()) return false;
	TestTrue(TEXT("Committing the dodge applies chase cooldown immediately"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_Pursuit));
	TestFalse(TEXT("Chase never consumes ordinary-attack cooldown"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_Normal));
	TestFalse(TEXT("Chase never consumes three-combo cooldown"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_ThreeCombo));
	Fixture.Cancel();
	TestFalse(TEXT("A cancelled committed chase cannot restart during cooldown"), Fixture.TryStart());
	for (bool bThree : { false, true })
	{
		TestTrue(TEXT("Ordinary abilities can still activate during chase cooldown"), Fixture.StartOrdinary(bThree));
		Fixture.CancelOrdinary(bThree);
	}
	Fixture.TickFor(0.3f);
	Fixture.PlaceTarget(Fixture.GetBoss()->GetActorForwardVector() * 550.f);
	TestFalse(TEXT("Chase cooldown expires at its configured duration"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_Pursuit));
	TestTrue(TEXT("The ordinary attack still has its longer cooldown"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_Normal));
	TestTrue(TEXT("The three-combo still has its longer cooldown"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_ThreeCombo));
	TestTrue(TEXT("Ordinary attack cooldowns do not prevent a new chase"), Fixture.CanActivate());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossChaseBranchExitTest,
	"First.Combat.BossPursuit.CommittedChaseSurvivesDistanceBranchExit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossChaseBranchExitTest::RunTest(const FString& Parameters)
{
	FirstBossDashFollowupTests::FFixture Fixture(*this);
	if (!Fixture.IsReady() || !Fixture.BeginDodge()) return false;
	// These keys are updated when the real behavior tree switches away from
	// MoveTo because the dash shortened the gap and acquired Attacking.
	Fixture.SetBlackboardBool(TEXT("bIsBusy"), true);
	Fixture.SetBlackboardBool(TEXT("bPlayerTooFar"), false);
	Fixture.SetBlackboardBool(TEXT("bShouldChase"), false);
	// Direct event-driven callers own their supplied target independently of
	// any blackboard. Only the BT task interprets a changed blackboard target.
	Fixture.ClearTarget();
	TestTrue(TEXT("Leaving chase selection after commitment still reaches the followup"), Fixture.WaitForFollowup());
	TestEqual(TEXT("The branch state change does not release Attacking between phases"), Fixture.BusyDrops(), 0);
	Fixture.TickFor(Fixture.GetBoss()->GetNormalAttackMontage()->GetPlayLength() + 0.6f);
	TestFalse(TEXT("The autonomous chase ability finishes its own lifecycle"), Fixture.IsActive());
	TestEqual(TEXT("Only one dodge and one attack were committed"), Fixture.StartedCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossChaseDamageAndDefenseTest,
	"First.Combat.BossPursuit.ChaseFollowupDamageAndParry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossChaseDamageAndDefenseTest::RunTest(const FString& Parameters)
{
	for (bool bCustomDamage : { false, true })
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) return false;
		FFirstBossPursuitSettings& Settings = Fixture.GetBoss()->PursuitSettings;
		if (bCustomDamage)
		{
			if (!Fixture.UseSavedCustomFollowup()) return false;
			Settings.bUseNormalAttackDamage = false;
			Settings.AttackDamage = 37.f;
			Settings.bUseNormalAttackDefense = false;
			Settings.AttackDefenseData.bParryable = true;
			Settings.AttackDefenseData.ParryPoiseDamage = 25.f;
		}
		if (!Fixture.BeginDodge()) return false;
		const float DuringDodge = Fixture.TargetHealth();
		Fixture.HitTarget();
		TestEqual(TEXT("The dodge movement phase cannot inflict attack damage"), Fixture.TargetHealth(), DuringDodge);
		if (!TestTrue(TEXT("The real dodge reaches its damage-bearing followup"), Fixture.WaitForFollowup())) continue;
		const float BeforeHit = Fixture.TargetHealth();
		Fixture.HitTarget();
		TestEqual(TEXT("The chase followup respects ordinary or custom damage selection"), BeforeHit - Fixture.TargetHealth(),
			bCustomDamage ? Settings.AttackDamage : Fixture.GetBoss()->GetNormalAttackDamage());
		Fixture.EnableTargetParry();
		const float BeforeParry = Fixture.TargetHealth();
		Fixture.HitTarget();
		TestEqual(TEXT("The followup uses the real defense resolver and can be parried"), Fixture.TargetHealth(), BeforeParry);
		TestFalse(TEXT("A parried followup immediately ends independent chase"), Fixture.IsActive());
		TestFalse(TEXT("A parried followup removes its direction lock"), Fixture.HasTag(MyGameplayTags::Boss_Status_AttackDirectionLocked));
		Fixture.TickFor(2.f);
		TestEqual(TEXT("A parried followup cannot resurrect another attack"), Fixture.StartedCount(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossChaseBehaviorTreeTest,
	"First.Combat.BossPursuit.BehaviorTreeTaskWaitsForAbility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossChaseBehaviorTreeTest::RunTest(const FString& Parameters)
{
	FirstBossDashFollowupTests::FFixture Fixture(*this);
	if (!Fixture.IsReady() || !Fixture.RunChaseTree()) return false;
	Fixture.TickFor(0.2f);
	TestTrue(TEXT("The actual BT task starts pursuit with its selected target"), Fixture.IsActive());
	TestEqual(TEXT("The real BT starts only the dodge phase"), Fixture.DodgeCount(), 1);
	TestTrue(TEXT("The task remains InProgress during the dodge"), Fixture.TreeIsWaitingForAbility());
	if (!TestTrue(TEXT("An unrelated ability can coexist with the chase task"), Fixture.StartUnrelated())) return false;
	Fixture.CancelUnrelated();
	Fixture.TickFor(0.01f);
	TestTrue(TEXT("Another ability's ended event cannot finish this task"), Fixture.TreeIsWaitingForAbility());
	TestTrue(TEXT("Another ability's ended event cannot cancel pursuit"), Fixture.IsActive());
	// These are the values the real state/distance services publish when
	// pursuit owns Attacking and its dash closes the gap. LowerPriority
	// observation must not abort the already selected ability task.
	Fixture.SetBlackboardBool(TEXT("bIsBusy"), true);
	Fixture.SetBlackboardBool(TEXT("bPlayerTooFar"), false);
	Fixture.SetBlackboardBool(TEXT("bShouldChase"), false);
	TestTrue(TEXT("The committed BT task reaches the followup despite changed selection conditions"), Fixture.WaitForFollowup());
	TestTrue(TEXT("The task keeps waiting during the followup instead of returning after the dodge"), Fixture.TreeIsWaitingForAbility());
	TestEqual(TEXT("Task execution keeps Attacking continuous between phases"), Fixture.BusyDrops(), 0);
	Fixture.PlaceTarget(FVector(1200.f, 0.f, 0.f));
	if (!TestTrue(TEXT("The followup completes its full ability lifecycle"), Fixture.WaitForAbilityEnd())) return false;
	Fixture.SetBlackboardBool(TEXT("bIsBusy"), false);
	Fixture.TickFor(0.03f);
	TestFalse(TEXT("The task stops waiting when its exact ability ends"), Fixture.TreeIsWaitingForAbility());
	TestTrue(TEXT("The tree returns to its ordinary Move To after the skill finishes"), Fixture.TreeIsMoving());
	TestEqual(TEXT("One task execution plays exactly one dodge and one followup"), Fixture.StartedCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossChasePreemptsMovementTest,
	"First.Combat.BossPursuit.BehaviorTreeChasePreemptsMoveTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossChasePreemptsMovementTest::RunTest(const FString& Parameters)
{
	for (bool bCooldown : { false, true })
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady()) return false;
		Fixture.GetBoss()->GetCharacterMovement()->MaxWalkSpeed = 100.f;
		Fixture.PlaceTarget(FVector(bCooldown ? 650.f : 1000.f, 0.f, 0.f));
		if (bCooldown) Fixture.SetBossTag(MyGameplayTags::Boss_Cooldown_Attack_Pursuit, true);
		const FVector Start = Fixture.GetBoss()->GetActorLocation();
		if (!Fixture.RunChaseTree()) return false;
		Fixture.TickFor(0.15f);
		TestFalse(TEXT("Cooldown or excessive distance prevents skill branch selection"), Fixture.IsActive());
		TestTrue(TEXT("The unavailable skill falls through to the real Move To task"), Fixture.TreeIsMoving());
		TestTrue(TEXT("The real Move To keeps running when pursuit is unavailable"),
			FVector::Dist2D(Start, Fixture.GetBoss()->GetActorLocation()) > 1.f);
		if (bCooldown) Fixture.SetBossTag(MyGameplayTags::Boss_Cooldown_Attack_Pursuit, false);
		else Fixture.PlaceTarget(FVector(600.f, 0.f, 0.f));
		Fixture.TickFor(0.2f);
		TestTrue(bCooldown ? TEXT("Cooldown expiry preempts an already running Move To") :
			TEXT("Entering the trigger range preempts an already running Move To"), Fixture.IsActive());
		TestTrue(TEXT("The higher-priority ability task now owns tree execution"), Fixture.TreeIsWaitingForAbility());
		TestEqual(TEXT("Movement preemption starts one forward dodge"), Fixture.DodgeCount(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossChaseTaskCancellationTest,
	"First.Combat.BossPursuit.BehaviorTreeTaskCancellationCleansUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossChaseTaskCancellationTest::RunTest(const FString& Parameters)
{
	for (int32 Scenario = 0; Scenario < 4; ++Scenario)
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.RunChaseTree()) return false;
		Fixture.TickFor(0.2f);
		if (!TestTrue(TEXT("A real BT ability task is active before cancellation"), Fixture.TreeIsWaitingForAbility())) return false;
		Fixture.SetBlackboardBool(TEXT("bShouldChase"), false);
		if (Scenario == 0) Fixture.Cancel();
		else if (Scenario == 1) Fixture.ClearTarget();
		else if (Scenario == 2) Fixture.ReplaceTarget();
		else
		{
			if (!TestTrue(TEXT("An unrelated ability is active before stopping the tree"), Fixture.StartUnrelated())) return false;
			Fixture.StopTree();
		}
		Fixture.TickFor(0.15f);
		TestFalse(FString::Printf(TEXT("Task cancellation scenario %d releases the pursuit ability"), Scenario), Fixture.IsActive());
		TestFalse(TEXT("The task does not remain stuck after cancellation"), Fixture.TreeIsWaitingForAbility());
		TestFalse(TEXT("The aborted skill releases its direction lock"), Fixture.HasTag(MyGameplayTags::Boss_Status_AttackDirectionLocked));
		if (Scenario == 3)
		{
			TestTrue(TEXT("Stopping the tree cancels only its owned skill, not unrelated active abilities"), Fixture.UnrelatedIsActive());
			Fixture.CancelUnrelated();
		}
		Fixture.TickFor(1.f);
		TestEqual(TEXT("A task cancellation cannot resurrect its followup"), Fixture.NormalCount(), 0);
		TestEqual(TEXT("Task cleanup releases Attacking exactly once"), Fixture.BusyDrops(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossRetaliationPressureTest,
	"First.Combat.BossRetaliation.RealDamageThresholdAndResetRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossRetaliationPressureTest::RunTest(const FString& Parameters)
{
	FirstBossDashFollowupTests::FFixture Fixture(*this);
	if (!Fixture.IsReady() || !Fixture.InitializeRetaliation()) return false;
	ABossCharacter* Boss = Fixture.GetBoss();
	UFirstBossRetaliationComponent* Retaliation = Boss->RetaliationComponent;
	TestEqual(TEXT("The configured default threshold is five actual hits"), Retaliation->HitsRequired, 5);
	TestEqual(TEXT("The configured default inactivity reset is three seconds"), Retaliation->HitResetDelay, 3.f);
	Fixture.DamageBoss(1.f, false);
	Fixture.DamageBoss(0.f);
	TestEqual(TEXT("Damage without an attack source and zero-damage hits do not build pressure"), Retaliation->HitCount, 0);
	Fixture.DamageBossRepeatedly(4);
	Fixture.TickFor(0.02f);
	TestEqual(TEXT("Four real sword damage executions record four hits"), Retaliation->HitCount, 4);
	TestFalse(TEXT("Four hits do not publish a retaliation request"), Retaliation->bRetaliationPending);
	Fixture.TickFor(3.05f);
	TestEqual(TEXT("Three seconds without a hit expires the partial count"), Retaliation->HitCount, 0);
	Fixture.DamageBossRepeatedly(5);
	TestEqual(TEXT("The fifth damage execution reaches the threshold"), Retaliation->HitCount, 5);
	TestFalse(TEXT("The fifth hit cannot publish before the same-hit poise damage finishes"), Retaliation->bRetaliationPending);
	Fixture.TickFor(0.02f);
	TestTrue(TEXT("The next tick publishes the request for the current combat target"),
		Retaliation->CanRetaliateAgainst(Fixture.GetTarget()));
	TestEqual(TEXT("The component alone does not start an attack montage"), Fixture.NormalCount(), 0);
	if (!TestTrue(TEXT("An ordinary attack can consume the pressure by actually starting"), Fixture.StartOrdinary(false))) return false;
	TestEqual(TEXT("Starting an attack resets the counter"), Retaliation->HitCount, 0);
	TestFalse(TEXT("Starting an attack consumes the pending request"), Retaliation->bRetaliationPending);
	Fixture.DamageBoss();
	TestEqual(TEXT("A hit received during an attack does not build new pressure"), Retaliation->HitCount, 0);
	Fixture.CancelOrdinary(false);
	Fixture.DamageBossRepeatedly(2);
	TestEqual(TEXT("Pressure can rebuild after the attack ends"), Retaliation->HitCount, 2);
	Retaliation->UpdateCombatContext(Fixture.GetTarget(), false);
	TestEqual(TEXT("Leaving combat immediately clears partial pressure"), Retaliation->HitCount, 0);
	Fixture.DamageBoss();
	TestEqual(TEXT("Damage outside a live combat context does not build pressure"), Retaliation->HitCount, 0);
	Retaliation->UpdateCombatContext(Fixture.GetTarget(), true);
	Fixture.DamageBossRepeatedly(5);
	Fixture.TickFor(0.02f);
	TestTrue(TEXT("Reentering combat can build a new request"), Retaliation->bRetaliationPending);
	Fixture.TickFor(3.05f);
	TestFalse(TEXT("An unused request also expires after three seconds without a hit"), Retaliation->bRetaliationPending);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossRetaliationProtectedStateTest,
	"First.Combat.BossRetaliation.HitReactCancellationAndProtectedStatePriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossRetaliationProtectedStateTest::RunTest(const FString& Parameters)
{
	for (int32 Scenario = 0; Scenario < 4; ++Scenario)
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.InitializeRetaliation(true)) return false;
		ABossCharacter* Boss = Fixture.GetBoss();
		UFirstBossRetaliationComponent* Retaliation = Boss->RetaliationComponent;
		if (Scenario == 0)
		{
			Fixture.SetBossTag(MyGameplayTags::Boss_Status_HitReactWindow, true);
			Fixture.DamageBossRepeatedly(5);
			Fixture.TickFor(0.02f);
			if (!TestTrue(TEXT("Real damage activated the ordinary hit-react ability"), Fixture.HitReactIsActive()) ||
				!TestTrue(TEXT("Five hits during ordinary hit-react can request retaliation"), Retaliation->bRetaliationPending)) return false;
			if (!TestTrue(TEXT("An unrelated ability runs alongside hit-react"), Fixture.StartUnrelated())) return false;
			TestTrue(TEXT("The BT preparation can end ordinary hit-react early"), Retaliation->PrepareRetaliation(Fixture.GetTarget()));
			TestFalse(TEXT("Preparation cancels the actual ordinary hit-react ability"), Fixture.HitReactIsActive());
			TestFalse(TEXT("The cancelled ability releases its own Staggered tag"), Fixture.HasTag(MyGameplayTags::Boss_Status_Staggered));
			TestTrue(TEXT("Preparation does not cancel an unrelated active ability"), Fixture.UnrelatedIsActive());
			Fixture.CancelUnrelated();
		}
		else if (Scenario == 1)
		{
			Fixture.DamageBossRepeatedly(5);
			TestFalse(TEXT("The fifth hit's publication is still deferred before poise damage"), Retaliation->bRetaliationPending);
			TestEqual(TEXT("The same hit really breaks the BOSS's poise"), Boss->ApplyPoiseDamage(100.f, Fixture.GetTarget()), EFirstPoiseDamageResult::Broken);
			Fixture.TickFor(0.02f);
			TestTrue(TEXT("The production executable ability owns the resulting break state"), Fixture.ExecutableIsActive());
			TestEqual(TEXT("Breaking poise clears the just-reached fifth-hit threshold"), Retaliation->HitCount, 0);
			TestFalse(TEXT("The deferred callback cannot publish a stale request after poise break"), Retaliation->bRetaliationPending);
			TestFalse(TEXT("BT preparation cannot release executable stagger"), Retaliation->PrepareRetaliation(Fixture.GetTarget()));
			TestTrue(TEXT("The protected executable state remains active"), Fixture.HasTag(MyGameplayTags::Boss_Status_Executable));
		}
		else if (Scenario == 2)
		{
			Fixture.DamageBossRepeatedly(5);
			Fixture.TickFor(0.02f);
			Fixture.StartParryStagger();
			if (!TestTrue(TEXT("The production parry-stagger ability is active"), Fixture.ParryStaggerIsActive())) return false;
			TestFalse(TEXT("Entering parry stagger consumes pending pressure"), Retaliation->bRetaliationPending);
			Fixture.DamageBossRepeatedly(5);
			Fixture.TickFor(0.02f);
			TestEqual(TEXT("Hits during parry stagger do not build retaliation pressure"), Retaliation->HitCount, 0);
			TestFalse(TEXT("BT preparation cannot cancel a parry stagger"), Retaliation->PrepareRetaliation(Fixture.GetTarget()));
			TestTrue(TEXT("The parry stagger remains owned by its ability"), Fixture.ParryStaggerIsActive());
		}
		else
		{
			if (!TestTrue(TEXT("An ordinary attack starts before the interrupting hit"), Fixture.StartOrdinary(false))) return false;
			Fixture.SetBossTag(MyGameplayTags::Boss_Status_HitReactWindow, true);
			Fixture.DamageBoss();
			TestTrue(TEXT("The damage's synchronous health callback activates ordinary hit-react"), Fixture.HitReactIsActive());
			TestFalse(TEXT("That callback already cancelled the attack before the damage event was sent"), Fixture.HasTag(MyGameplayTags::Boss_Status_Attacking));
			TestEqual(TEXT("The pre-damage snapshot still excludes this attack-interrupting hit"), Retaliation->HitCount, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossRetaliationBehaviorTreeTest,
	"First.Combat.BossRetaliation.BehaviorTreePreemptsWaitAndKeepsAttackSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossRetaliationBehaviorTreeTest::RunTest(const FString& Parameters)
{
	for (int32 Scenario = 0; Scenario < 2; ++Scenario)
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.InitializeRetaliation()) return false;
		Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
		if (!Fixture.RunRetaliationTree()) return false;
		Fixture.TickFor(0.1f);
		if (!TestEqual(TEXT("The original BT select-and-activate chain launches its initial normal attack"), Fixture.NormalCount(), 1)) return false;
		UFirstBossRetaliationComponent* Retaliation = Fixture.GetBoss()->RetaliationComponent;
		if (Scenario == 0)
		{
			// Let the real 4-second ability cooldown expire while the branch's
			// deliberately longer 20-second cooldown still blocks normal entry.
			Fixture.TickFor(4.2f);
			if (!TestTrue(TEXT("The attack ends into the real fallback Wait task"), Fixture.TreeIsWaiting())) return false;
			TestFalse(TEXT("The actual attack ability cooldown has expired"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_Normal));
			Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
			Fixture.DamageBossRepeatedly(4);
			Fixture.TickFor(0.15f);
			TestEqual(TEXT("Four hits leave the existing BT cooldown and fallback wait intact"), Fixture.NormalCount(), 1);
			Fixture.ClearAttackSelection();
			Fixture.DamageBoss();
			Fixture.TickFor(0.25f);
			TestEqual(TEXT("The fifth hit preempts the lower-priority Wait and starts a second real attack"), Fixture.NormalCount(), 2);
			TestEqual(TEXT("The original select task writes its range-eligible normal attack again"), Fixture.SelectedAttackTag(), MyGameplayTags::Boss_Ability_Attack_Normal.GetTag().GetTagName());
			TestEqual(TEXT("Actual attack activation consumes the pending hit counter"), Retaliation->HitCount, 0);
			TestFalse(TEXT("Actual attack activation clears the request"), Retaliation->bRetaliationPending);
			Fixture.TickFor(0.3f);
			TestEqual(TEXT("A single threshold cannot restart an already-running attack"), Fixture.NormalCount(), 2);
		}
		else
		{
			Fixture.CancelOrdinary(false);
			Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
			TestTrue(TEXT("Cancelling the animation preserves the skill's real cooldown"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_Normal));
			Fixture.DamageBossRepeatedly(5);
			Fixture.TickFor(0.25f);
			TestTrue(TEXT("Pressure is pending while all attack options are unavailable"), Retaliation->bRetaliationPending);
			TestEqual(TEXT("A retaliation request cannot bypass the selected ability's own cooldown"), Fixture.NormalCount(), 1);
			Fixture.TickFor(3.1f);
			TestFalse(TEXT("An unfulfilled request expires instead of firing later without new hits"), Retaliation->bRetaliationPending);
			TestEqual(TEXT("The expired request never launches another attack"), Fixture.NormalCount(), 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossRetaliationRecoveryPreemptionTest,
	"First.Combat.BossRetaliation.NormalCooldownDoesNotPreemptRecoveryWithoutRetaliation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossRetaliationRecoveryPreemptionTest::RunTest(const FString& Parameters)
{
	for (int32 Scenario = 0; Scenario < 2; ++Scenario)
	{
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.InitializeRetaliation()) return false;
		UFirstBossRetaliationComponent* Retaliation = Fixture.GetBoss()->RetaliationComponent;
		Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
		if (Scenario == 1) Retaliation->bEnabled = false;
		// The real 2-second branch interval and 4-second ability cooldown both
		// expire while the lower-priority recovery task is still in progress.
		if (!Fixture.RunRetaliationTree(2.f, Scenario == 0 ? 60.f : 6.f)) return false;
		Fixture.TickFor(0.1f);
		if (!TestEqual(TEXT("A normal decision launches the initial attack without pressure"), Fixture.NormalCount(), 1)) return false;
		if (Scenario == 0)
		{
			Fixture.TickFor(6.25f);
			TestEqual(TEXT("No incoming attacks means no retaliation pressure"), Retaliation->HitCount, 0);
			TestFalse(TEXT("The actual ability cooldown has expired during recovery"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_Normal));
			if (!TestEqual(TEXT("Ordinary cooldown expiry must not preempt recovery and restart attacking without any hits"), Fixture.NormalCount(), 1)) return false;
			TestTrue(TEXT("The same lower-priority recovery task continues after both cooldowns expire"), Fixture.TreeIsWaiting());
			TestTrue(TEXT("The target remains within the original attack option's usable range"),
				FVector::Dist(Fixture.GetBoss()->GetActorLocation(), Fixture.GetTarget()->GetActorLocation()) < 400.f);
			Retaliation->bEnabled = false;
			Fixture.DamageBossRepeatedly(5);
			Fixture.TickFor(0.25f);
			TestEqual(TEXT("Disabling retaliation leaves recovery uninterrupted even after five hits"), Fixture.NormalCount(), 1);
			TestEqual(TEXT("Disabled retaliation does not accumulate pressure"), Retaliation->HitCount, 0);
			Retaliation->bEnabled = true;
			Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
			Fixture.DamageBossRepeatedly(4);
			Fixture.TickFor(0.25f);
			TestEqual(TEXT("Four valid hits still let the active recovery task continue"), Fixture.NormalCount(), 1);
			TestTrue(TEXT("Recovery stays active below the retaliation threshold"), Fixture.TreeIsWaiting());
			Fixture.DamageBoss();
			Fixture.TickFor(0.25f);
			TestEqual(TEXT("The fifth valid hit can still preempt recovery through the original attack chain"), Fixture.NormalCount(), 2);
			TestEqual(TEXT("The started retaliation consumes its pressure"), Retaliation->HitCount, 0);
		}
		else
		{
			Fixture.TickFor(4.2f);
			if (!TestEqual(TEXT("With retaliation disabled, normal cooldown expiry still cannot interrupt an unfinished recovery"), Fixture.NormalCount(), 1)) return false;
			TestTrue(TEXT("The finite recovery task is still running before its own completion"), Fixture.TreeIsWaiting());
			Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
			Fixture.TickFor(2.1f);
			TestEqual(TEXT("Natural recovery completion performs a new decision and permits an ordinary attack"), Fixture.NormalCount(), 2);
			TestEqual(TEXT("Normal decisions remain independent of retaliation hit counts"), Retaliation->HitCount, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossRetaliationBusyRetryCooldownTest,
	"First.Combat.BossRetaliation.BusyRootRetriesPreserveOriginalCooldownRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossRetaliationBusyRetryCooldownTest::RunTest(const FString& Parameters)
{
	bool EnteredRecovery[2] = {};
	int32 FollowupsAfterRecovery[2] = {};
	for (int32 Scenario = 0; Scenario < 2; ++Scenario)
	{
		const bool bLegacyCooldown = Scenario == 0;
		FirstBossDashFollowupTests::FFixture Fixture(*this);
		if (!Fixture.IsReady() || !Fixture.InitializeRetaliation()) return false;
		Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
		if (!TestTrue(TEXT("The saved three-combo lasts longer than the ordinary two-second branch interval"),
			Fixture.GetBoss()->GetThreeComboMontage()->GetPlayLength() > 2.5f) ||
			!TestTrue(TEXT("A real long three-combo starts before the busy root retries"), Fixture.StartOrdinary(true))) return false;
		// A still-unused ordinary attack is eligible by range and skill
		// cooldown, but GAS must reject it while the three-combo is active.
		// There is no running fallback task during Busy, matching BT_Boss.
		if (!Fixture.RunRetaliationTree(2.f, 1.5f, true, bLegacyCooldown)) return false;
		float AttackElapsed = 0.f;
		for (int32 Step = 0; Step < 2400 && Fixture.OrdinaryIsActive(true); ++Step)
		{
			Fixture.TickFor(0.005f);
			AttackElapsed += 0.005f;
		}
		if (!TestFalse(TEXT("The real three-combo finishes without being cancelled"), Fixture.OrdinaryIsActive(true))) return false;
		TestTrue(TEXT("Root retries occurred across multiple ordinary cooldown intervals"), AttackElapsed > 4.f);
		Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
		Fixture.TickFor(0.1f);
		EnteredRecovery[Scenario] = Fixture.TreeIsWaiting();
		AddInfo(FString::Printf(TEXT("%s cooldown: long attack ended after %.3f s, recovery entered=%d, ordinary followups=%d"),
			bLegacyCooldown ? TEXT("Original engine") : TEXT("Retaliation-aware"), AttackElapsed,
			EnteredRecovery[Scenario], Fixture.NormalCount()));
		TestTrue(FString::Printf(TEXT("%s cooldown lets the original recovery branch run after the long attack"),
			bLegacyCooldown ? TEXT("Original engine") : TEXT("Retaliation-aware")), EnteredRecovery[Scenario]);
		TestEqual(TEXT("The long attack's ending must not immediately chain into an unused attack"), Fixture.NormalCount(), 0);
		TestEqual(TEXT("This recovery path requires no retaliation pressure"), Fixture.GetBoss()->RetaliationComponent->HitCount, 0);
		Fixture.TickFor(1.6f);
		FollowupsAfterRecovery[Scenario] = Fixture.NormalCount();
		TestEqual(TEXT("The existing selection chain can attack normally when recovery finishes"), FollowupsAfterRecovery[Scenario], 1);
	}
	TestEqual(TEXT("The replacement preserves the original decorator's post-combo recovery entry"), EnteredRecovery[1], EnteredRecovery[0]);
	TestEqual(TEXT("The replacement preserves ordinary attack availability after recovery"), FollowupsAfterRecovery[1], FollowupsAfterRecovery[0]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossPostAttackRecoveryTimingTest,
	"First.Combat.BossRetaliation.PostAttackRecoveryStartsAfterLongAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossPostAttackRecoveryTimingTest::RunTest(const FString& Parameters)
{
	FirstBossDashFollowupTests::FFixture Fixture(*this);
	if (!Fixture.IsReady() || !Fixture.InitializeRetaliation()) return false;
	Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
	// Start before the tree to also exercise binding to an already-active GA.
	if (!TestTrue(TEXT("A real long three-combo starts"), Fixture.StartOrdinary(true)) ||
		!Fixture.RunRetaliationTree(2.f, 0.1f, true, false, true, 3.f, 3.f)) return false;
	Fixture.TickFor(0.05f);
	UBTDecorator_BossAttackCooldown* Recovery = Fixture.RecoveryNode();
	if (!TestNotNull(TEXT("The tree owns an instanced recovery decorator"), Recovery)) return false;
	Fixture.TickFor(3.2f);
	TestTrue(TEXT("The real three-combo is still playing after three seconds"), Fixture.OrdinaryIsActive(true));
	TestEqual(TEXT("Playing time does not consume or sample the recovery interval"), Recovery->GetSampledRecoveryDuration(), 0.f);
	TestEqual(TEXT("Busy root retries cannot start an unused ordinary attack"), Fixture.NormalCount(), 0);
	if (!TestTrue(TEXT("The long attack completes normally"), Fixture.WaitForOrdinaryEnd(true))) return false;
	Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
	Fixture.TickFor(0.02f);
	TestEqual(TEXT("The completed attack samples the configured full interval"), Recovery->GetSampledRecoveryDuration(), 3.f);
	TestTrue(TEXT("Nearly all three seconds remain after the actual end"), Recovery->GetRecoveryTimeRemaining() > 2.9f);
	Fixture.TickFor(2.85f);
	TestTrue(TEXT("The interval is still active before three seconds after the end"), Recovery->GetRecoveryTimeRemaining() > 0.f);
	TestFalse(TEXT("Natural root retries cannot enter the attack branch early"), Fixture.RecoveryAllowsEntry());
	TestEqual(TEXT("No ordinary attack begins before recovery completes"), Fixture.NormalCount(), 0);
	Fixture.TickFor(0.3f);
	TestEqual(TEXT("A natural decision can attack after the full recovery interval"), Fixture.NormalCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossPostAttackRecoveryRandomTest,
	"First.Combat.BossRetaliation.PostAttackRandomRecoveryPreservesRetaliationAndNaturalDecisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossPostAttackRecoveryRandomTest::RunTest(const FString& Parameters)
{
	FirstBossDashFollowupTests::FFixture Fixture(*this);
	if (!Fixture.IsReady() || !Fixture.InitializeRetaliation()) return false;
	Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
	if (!Fixture.RunRetaliationTree(2.f, 60.f, false, false, true, 3.f, 6.f)) return false;
	Fixture.TickFor(0.1f);
	if (!TestEqual(TEXT("The first ordinary attack has no added initial delay"), Fixture.NormalCount(), 1) ||
		!TestTrue(TEXT("The first attack completes"), Fixture.WaitForOrdinaryEnd(false))) return false;
	Fixture.TickFor(0.02f);
	UBTDecorator_BossAttackCooldown* Recovery = Fixture.RecoveryNode();
	if (!TestNotNull(TEXT("The real random-recovery node is available"), Recovery)) return false;
	const float FirstSample = Recovery->GetSampledRecoveryDuration();
	TestTrue(TEXT("The sampled duration is within the configured three-to-six-second range"), FirstSample >= 3.f && FirstSample <= 6.f);
	TestTrue(TEXT("Actual skill cooldown still blocks premature activation"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_Normal));
	for (int32 Query = 0; Query < 12; ++Query)
	{
		TestFalse(TEXT("Repeated branch queries remain blocked during recovery"), Fixture.RecoveryAllowsEntry());
		TestFalse(TEXT("A rejected real ability attempt does not start a new attack"), Fixture.StartOrdinary(false));
		TestEqual(TEXT("Queries and failed activation attempts do not reroll the duration"), Recovery->GetSampledRecoveryDuration(), FirstSample);
	}
	Fixture.TickFor(1.8f);
	TestFalse(TEXT("The actual skill cooldown expires before the post-attack recovery"), Fixture.HasTag(MyGameplayTags::Boss_Cooldown_Attack_Normal));
	TestTrue(TEXT("Recovery still has time remaining when pressure is applied"), Recovery->GetRecoveryTimeRemaining() > 0.8f);
	Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
	Fixture.DamageBossRepeatedly(4);
	Fixture.TickFor(0.15f);
	TestEqual(TEXT("Four hits cannot bypass post-attack recovery"), Fixture.NormalCount(), 1);
	Fixture.DamageBoss();
	Fixture.TickFor(0.25f);
	if (!TestEqual(TEXT("The fifth valid hit still bypasses recovery through the real selection chain"), Fixture.NormalCount(), 2) ||
		!TestTrue(TEXT("The retaliatory attack finishes"), Fixture.WaitForOrdinaryEnd(false))) return false;
	Fixture.TickFor(0.02f);
	const float SecondSample = Recovery->GetSampledRecoveryDuration();
	TestTrue(TEXT("The next completed attack also samples within the requested range"), SecondSample >= 3.f && SecondSample <= 6.f);
	Fixture.TickFor(SecondSample + 0.3f);
	TestEqual(TEXT("Ordinary recovery expiry cannot preempt the unfinished fallback task"), Fixture.NormalCount(), 2);
	TestTrue(TEXT("The original long recovery task remains active"), Fixture.TreeIsWaiting());
	TestTrue(TEXT("The recorded recovery has expired"), Recovery->GetRecoveryTimeRemaining() <= 0.f);
	// Force real root decisions with no range-eligible attack. Unlike rejected
	// condition queries, these execute and fail inside the attack sequence.
	Fixture.PlaceTarget(FVector(600.f, 0.f, 0.f));
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		Fixture.TreeComponent()->RestartTree(EBTRestartMode::ForceReevaluateRootNode);
		Fixture.TickFor(0.03f);
		TestEqual(TEXT("Failed selection does not sample another recovery interval"), Recovery->GetSampledRecoveryDuration(), SecondSample);
		TestTrue(TEXT("Failed selection does not extend the expired deadline"), Recovery->GetRecoveryTimeRemaining() <= 0.f);
	}
	Fixture.PlaceTarget(FVector(220.f, 0.f, 0.f));
	Fixture.TreeComponent()->RestartTree(EBTRestartMode::ForceReevaluateRootNode);
	Fixture.TickFor(0.1f);
	TestEqual(TEXT("A new natural decision can attack as soon as an option becomes usable"), Fixture.NormalCount(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossPostAttackRecoveryCleanupTest,
	"First.Combat.BossRetaliation.PostAttackRecoveryCancellationAndTreeRestart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossPostAttackRecoveryCleanupTest::RunTest(const FString& Parameters)
{
	FirstBossDashFollowupTests::FFixture Fixture(*this);
	if (!Fixture.IsReady() || !Fixture.InitializeRetaliation()) return false;
	Fixture.PlaceTarget(FVector(600.f, 0.f, 0.f));
	if (!Fixture.RunRetaliationTree(2.f, 60.f, false, false, true, 3.f, 3.f)) return false;
	Fixture.TickFor(0.05f);
	UBTDecorator_BossAttackCooldown* Recovery = Fixture.RecoveryNode();
	if (!TestNotNull(TEXT("A real recovery instance was bound"), Recovery) ||
		!TestTrue(TEXT("An ordinary ability can start directly"), Fixture.StartOrdinary(false))) return false;
	Fixture.TickFor(0.1f);
	Fixture.CancelOrdinary(false);
	Fixture.TickFor(0.02f);
	TestEqual(TEXT("A cancelled attack also samples recovery from its actual end"), Recovery->GetSampledRecoveryDuration(), 3.f);
	TestTrue(TEXT("Cancellation leaves a full recovery interval"), Recovery->GetRecoveryTimeRemaining() > 2.9f);
	if (!TestTrue(TEXT("A separate real three-combo starts for the pending-cleanup case"), Fixture.StartOrdinary(true))) return false;
	Fixture.TickFor(0.1f);
	Fixture.CancelOrdinary(true);
	// Stop before the next-tick end confirmation can run, retaining the old
	// UObject solely so a stale callback would be observable rather than GC'd.
	TStrongObjectPtr<UBTDecorator_BossAttackCooldown> StoppedRecovery(Recovery);
	Fixture.StopTree();
	const float StoppedSample = StoppedRecovery->GetSampledRecoveryDuration();
	Fixture.TickFor(0.05f);
	TestEqual(TEXT("Stopping the tree removes the pending end-confirmation callback"), StoppedRecovery->GetSampledRecoveryDuration(), StoppedSample);
	TestTrue(TEXT("Destroying the tree clears its old recovery deadline"), StoppedRecovery->GetRecoveryTimeRemaining() <= 0.f);
	Fixture.SetBlackboardBool(TEXT("bShouldChase"), false);
	Fixture.TreeComponent()->RestartTree();
	Fixture.TickFor(0.05f);
	Recovery = Fixture.RecoveryNode();
	if (!TestNotNull(TEXT("Restarting the same tree rebinds a usable node instance"), Recovery)) return false;
	TestEqual(TEXT("A fresh tree lifetime has no stale sampled recovery"), Recovery->GetSampledRecoveryDuration(), 0.f);
	Fixture.TickFor(4.1f);
	if (!TestTrue(TEXT("The real ordinary cooldown permits another attack after restart"), Fixture.StartOrdinary(false))) return false;
	Fixture.TickFor(0.1f);
	Fixture.CancelOrdinary(false);
	Fixture.TickFor(0.02f);
	TestEqual(TEXT("The rebound listener observes cancellation in the new lifetime"), Recovery->GetSampledRecoveryDuration(), 3.f);
	TestTrue(TEXT("The new deadline is based on the new attack ending"), Recovery->GetRecoveryTimeRemaining() > 2.9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstBossPostAttackRecoveryHandoffTest,
	"First.Combat.BossRetaliation.PostAttackRecoveryExcludesPursuitAndCoalescesAbilityHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstBossPostAttackRecoveryHandoffTest::RunTest(const FString& Parameters)
{
	FirstBossDashFollowupTests::FFixture Fixture(*this);
	if (!Fixture.IsReady() || !Fixture.InitializeRetaliation()) return false;
	Fixture.PlaceTarget(FVector(550.f, 0.f, 0.f));
	if (!Fixture.RunRetaliationTree(2.f, 60.f, false, false, true, 3.f, 3.f)) return false;
	Fixture.TickFor(0.05f);
	UBTDecorator_BossAttackCooldown* Recovery = Fixture.RecoveryNode();
	if (!TestNotNull(TEXT("The recovery node listens throughout independent pursuit"), Recovery) ||
		!Fixture.BeginDodge() || !TestTrue(TEXT("The real forward dodge and followup complete"), Fixture.WaitForAbilityEnd())) return false;
	Fixture.TickFor(0.02f);
	TestEqual(TEXT("An independent pursuit does not sample the ordinary branch's recovery"), Recovery->GetSampledRecoveryDuration(), 0.f);
	TestTrue(TEXT("Independent pursuit leaves no ordinary recovery deadline"), Recovery->GetRecoveryTimeRemaining() <= 0.f);
	if (!TestTrue(TEXT("An ordinary attack starts after pursuit"), Fixture.StartOrdinary(false))) return false;
	Fixture.TickFor(0.1f);
	const int32 DropsBeforeHandoff = Fixture.BusyDrops();
	Fixture.CancelOrdinary(false);
	if (!TestTrue(TEXT("A real second GA starts in the same call stack as the first GA ending"), Fixture.StartOrdinary(true))) return false;
	TestEqual(TEXT("The handoff really contains a temporary Attacking zero transition"), Fixture.BusyDrops(), DropsBeforeHandoff + 1);
	Fixture.TickFor(0.02f);
	TestEqual(TEXT("A same-stack ability handoff does not sample an intermediate recovery"), Recovery->GetSampledRecoveryDuration(), 0.f);
	TestTrue(TEXT("The continuation remains active"), Fixture.OrdinaryIsActive(true));
	if (!TestTrue(TEXT("The continuation finishes normally"), Fixture.WaitForOrdinaryEnd(true))) return false;
	Fixture.TickFor(0.02f);
	TestEqual(TEXT("Only the final end samples the configured recovery"), Recovery->GetSampledRecoveryDuration(), 3.f);
	TestTrue(TEXT("The final end starts the full interval"), Recovery->GetRecoveryTimeRemaining() > 2.9f);
	return true;
}

#endif
