#include "AI/Decorators/BTDecorator_BossAttackCooldown.h"

#include "AIController.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "Components/Combat/FirstBossRetaliationComponent.h"
#include "Controller/BossAIController.h"
#include "Engine/World.h"
#include "MyGameplayTags.h"

UBTDecorator_BossAttackCooldown::UBTDecorator_BossAttackCooldown(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("Boss Attack Cooldown");
	CoolDownTime = 2.f;
	bCreateNodeInstance = true;
	bNotifyTick = true;
	FlowAbortMode = EBTFlowAbortMode::LowerPriority;
	bAllowAbortNone = false;
	bAllowAbortLowerPri = true;
	bAllowAbortChildNodes = false;
}

bool UBTDecorator_BossAttackCooldown::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	const AAIController* Controller = OwnerComp.GetAIOwner();
	const ABossCharacter* Boss = Controller ? Cast<ABossCharacter>(Controller->GetPawn()) : nullptr;
	const UFirstAbilitySystemComponent* ASC = Boss ? Boss->GetFirstAbilitySystemComponent() : nullptr;
	AActor* Target = BB ? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor"))) : nullptr;
	if (!IsValid(Boss) || !Boss->HasAuthority() || !ASC || !IsValid(Target) ||
		Target->IsActorBeingDestroyed() || Target == Boss ||
		!BB->GetValueAsBool(TEXT("bShouldChase")) || BB->GetValueAsBool(TEXT("bProvokedByDamage")) ||
		!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_WeaponDrawn)) return false;
	// 普通选路保留旧 Cooldown 的尝试/失败计时：攻击中仍由 GA 阻止重复发动。
	// 若在这里提前拒绝 Attacking，长动画期间的失败尝试不再刷新间隔，收招后会立刻再选攻。
	const FGameplayTag BlockingTags[] = { MyGameplayTags::Boss_Status_DrawSword,
		MyGameplayTags::Boss_Status_Executable, MyGameplayTags::Boss_Status_BeingExecuted,
		MyGameplayTags::Boss_Status_Executed, MyGameplayTags::Shared_Status_Dead };
	for (FGameplayTag Tag : BlockingTags)
	{
		if (ASC->HasMatchingGameplayTag(Tag)) return false;
	}
	const UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
	if (TargetASC && TargetASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead)) return false;
	const ABossAIController* BossController = Cast<ABossAIController>(Controller);
	if (BossController && BossController->IsTargetNavigationBlocked(Target)) return false;
	const bool bRetaliationReady = Boss->RetaliationComponent && Boss->RetaliationComponent->CanRetaliateAgainst(Target);
	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Staggered) && !bRetaliationReady) return false;
	if (bUsePostAttackRecovery)
	{
		// 真正收招后才开始休息，不依赖瞬时结束的发动任务或选招失败来续时。
		if (ObservedAbilitySystem.Get() != ASC || Recovery.bTrackingAttack ||
			ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Attacking)) return false;
		return bRetaliationReady || OwnerComp.GetWorld()->GetTimeSeconds() >= Recovery.ReadyTime;
	}
	return bRetaliationReady || Super::CalculateRawConditionValue(OwnerComp, NodeMemory);
}

void UBTDecorator_BossAttackCooldown::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
	EBTMemoryInit::Type InitType) const
{
	Super::InitializeMemory(OwnerComp, NodeMemory, InitType);
	if (InitType == EBTMemoryInit::Initialize) Recovery = FRecoveryState();
}

void UBTDecorator_BossAttackCooldown::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
	EBTMemoryClear::Type CleanupType) const
{
	if (CleanupType == EBTMemoryClear::Destroy) Recovery = FRecoveryState();
	Super::CleanupMemory(OwnerComp, NodeMemory, CleanupType);
}

bool UBTDecorator_BossAttackCooldown::IsRecoveryAttack(const UGameplayAbility* Ability)
{
	return Ability && Ability->GetAssetTags().HasTag(MyGameplayTags::Boss_Ability_Attack) &&
		!Ability->GetAssetTags().HasTag(MyGameplayTags::Boss_Ability_Attack_Pursuit);
}

void UBTDecorator_BossAttackCooldown::OnInstanceCreated(UBehaviorTreeComponent& OwnerComp)
{
	Super::OnInstanceCreated(OwnerComp);
	StopObserving();
	TimeUntilCheck = 0.f;
	if (!bUsePostAttackRecovery) return;
	const AAIController* Controller = OwnerComp.GetAIOwner();
	const ABossCharacter* Boss = Controller ? Cast<ABossCharacter>(Controller->GetPawn()) : nullptr;
	UAbilitySystemComponent* ASC = Boss ? Boss->GetFirstAbilitySystemComponent() : nullptr;
	if (!Boss || !Boss->HasAuthority() || !ASC) return;
	ObservedAbilitySystem = ASC;
	ObservedWorld = OwnerComp.GetWorld();
	AbilityActivatedHandle = ASC->AbilityActivatedCallbacks.AddUObject(this, &ThisClass::HandleAbilityActivated);
	AttackTagHandle = ASC->RegisterGameplayTagEvent(MyGameplayTags::Boss_Status_Attacking,
		EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::HandleAttackTagChanged);
	// A restored tree may attach while a real attack is already in progress.
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.IsActive() && IsRecoveryAttack(Spec.Ability))
		{
			Recovery.bTrackingAttack = true;
			break;
		}
	}
	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Attacking))
	{
		Recovery.PendingEndTime = -1.0;
	}
	else if (Recovery.bTrackingAttack)
	{
		// Preserve a known end timestamp when restoring a stored subtree.
		if (Recovery.PendingEndTime < 0.0) Recovery.PendingEndTime = ObservedWorld->GetTimeSeconds();
		ConfirmEndTimer = ObservedWorld->GetTimerManager().SetTimerForNextTick(this, &ThisClass::ConfirmAttackEnded);
	}
}

void UBTDecorator_BossAttackCooldown::OnInstanceDestroyed(UBehaviorTreeComponent& OwnerComp)
{
	StopObserving();
	Super::OnInstanceDestroyed(OwnerComp);
}

void UBTDecorator_BossAttackCooldown::StopObserving()
{
	if (UAbilitySystemComponent* ASC = ObservedAbilitySystem.Get())
	{
		ASC->AbilityActivatedCallbacks.Remove(AbilityActivatedHandle);
		ASC->RegisterGameplayTagEvent(MyGameplayTags::Boss_Status_Attacking,
			EGameplayTagEventType::NewOrRemoved).Remove(AttackTagHandle);
	}
	if (UWorld* World = ObservedWorld.Get()) World->GetTimerManager().ClearTimer(ConfirmEndTimer);
	ConfirmEndTimer.Invalidate();
	AbilityActivatedHandle.Reset();
	AttackTagHandle.Reset();
	ObservedAbilitySystem.Reset();
	ObservedWorld.Reset();
}

void UBTDecorator_BossAttackCooldown::HandleAbilityActivated(UGameplayAbility* Ability)
{
	if (!bUsePostAttackRecovery || !IsRecoveryAttack(Ability)) return;
	// GAS adds OwnedTags before AbilityActivatedCallbacks, and increments the
	// spec's active count afterwards. Use the supplied ability, not IsActive().
	Recovery.bTrackingAttack = true;
	Recovery.PendingEndTime = -1.0;
	Recovery.SampledDuration = 0.f;
	Recovery.ReadyTime = 0.0;
	if (UWorld* World = ObservedWorld.Get()) World->GetTimerManager().ClearTimer(ConfirmEndTimer);
}

void UBTDecorator_BossAttackCooldown::HandleAttackTagChanged(FGameplayTag Tag, int32 Count)
{
	UWorld* World = ObservedWorld.Get();
	if (!bUsePostAttackRecovery || !World) return;
	if (Count > 0)
	{
		// Chained attacks can briefly remove and re-add Attacking in one call stack.
		World->GetTimerManager().ClearTimer(ConfirmEndTimer);
		Recovery.PendingEndTime = -1.0;
	}
	else if (Recovery.bTrackingAttack)
	{
		Recovery.PendingEndTime = World->GetTimeSeconds();
		ConfirmEndTimer = World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::ConfirmAttackEnded);
	}
}

void UBTDecorator_BossAttackCooldown::GetRecoveryRange(float& OutMin, float& OutMax) const
{
	const float A = FMath::IsFinite(RecoveryTimeMin) ? FMath::Max(0.f, RecoveryTimeMin) : 3.f;
	const float B = FMath::IsFinite(RecoveryTimeMax) ? FMath::Max(0.f, RecoveryTimeMax) : 6.f;
	OutMin = FMath::Min(A, B);
	OutMax = FMath::Max(A, B);
}

void UBTDecorator_BossAttackCooldown::ConfirmAttackEnded()
{
	ConfirmEndTimer.Invalidate();
	const UAbilitySystemComponent* ASC = ObservedAbilitySystem.Get();
	if (!bUsePostAttackRecovery || !ASC || !ObservedWorld.IsValid() || !Recovery.bTrackingAttack ||
		Recovery.PendingEndTime < 0.0 || ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Attacking)) return;
	float MinTime, MaxTime;
	GetRecoveryRange(MinTime, MaxTime);
	Recovery.SampledDuration = FMath::FRandRange(MinTime, MaxTime);
	Recovery.ReadyTime = Recovery.PendingEndTime + Recovery.SampledDuration;
	Recovery.PendingEndTime = -1.0;
	Recovery.bTrackingAttack = false;
	// No BT request here: normal recovery never aborts movement on its own.
}

float UBTDecorator_BossAttackCooldown::GetRecoveryTimeRemaining() const
{
	const UWorld* World = ObservedWorld.Get();
	return World ? static_cast<float>(FMath::Max(0.0, Recovery.ReadyTime - World->GetTimeSeconds())) : 0.f;
}

void UBTDecorator_BossAttackCooldown::OnNodeDeactivation(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type NodeResult)
{
	// Failed selections/retries must not postpone or reroll a sampled recovery.
	if (!bUsePostAttackRecovery) Super::OnNodeDeactivation(SearchData, NodeResult);
}

void UBTDecorator_BossAttackCooldown::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	// 普通间隔到期只解除入场限制，不能主动打断正在执行的后退/周旋。
	// 只有受压反击请求拥有主动抢占权；否则每次普通冷却到期都会抢回攻击分支。
	TimeUntilCheck -= DeltaSeconds;
	if (TimeUntilCheck > 0.f) return;
	TimeUntilCheck = 0.1f;
	const AAIController* Controller = OwnerComp.GetAIOwner();
	const ABossCharacter* Boss = Controller ? Cast<ABossCharacter>(Controller->GetPawn()) : nullptr;
	const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	const AActor* Target = BB ? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor"))) : nullptr;
	if (!Boss || !Boss->RetaliationComponent || !Boss->RetaliationComponent->CanRetaliateAgainst(Target)) return;
	ConditionalFlowAbort(OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged);
}

FString UBTDecorator_BossAttackCooldown::GetStaticDescription() const
{
	if (bUsePostAttackRecovery)
	{
		float MinTime, MaxTime;
		GetRecoveryRange(MinTime, MaxTime);
		return FString::Printf(TEXT("Recovery: %.2f - %.2f s after the whole attack ends (sampled once).\nCool Down Time is unused in this mode. Pursuit keeps its own cooldown.\nOnly a pending retaliation actively preempts movement and bypasses recovery."), MinTime, MaxTime);
	}
	return FString::Printf(TEXT("Normal interval: %s s; expiry does not interrupt recovery movement.\nOnly a pending retaliation actively preempts lower priority and bypasses this interval.\nKeep the attack-range decorator and skill cooldowns."),
		*CoolDownTime.ToString());
}

void UBTDecorator_BossAttackCooldown::DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
	EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const
{
	if (!bUsePostAttackRecovery)
	{
		Super::DescribeRuntimeValues(OwnerComp, NodeMemory, Verbosity, Values);
		return;
	}
	Values.Add(Recovery.bTrackingAttack ? TEXT("Recovery waits for the attack to end") :
		FString::Printf(TEXT("Sampled: %.2f s; remaining: %.2f s"), Recovery.SampledDuration, GetRecoveryTimeRemaining()));
}
