// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/BTService_UpdateBossState.h"

#include "AIController.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "Controller/BossAIController.h"

UBTService_UpdateBossState::UBTService_UpdateBossState()
{
	NodeName = TEXT("Update Boss State");

	// 默认：攻击中 / 拔剑中 视为"忙碌"；可在 BT 节点详情里增删。
	ActionBlockingTags.AddTag(MyGameplayTags::Boss_Status_Attacking);
	ActionBlockingTags.AddTag(MyGameplayTags::Boss_Status_DrawSword);
	ActionBlockingTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActionBlockingTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActionBlockingTags.AddTag(MyGameplayTags::Boss_Status_BeingExecuted);

	// 处决事务接管：破韧定格、处决演出与非致死起立期间旋转冻结。
	ExecutionControlTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ExecutionControlTags.AddTag(MyGameplayTags::Boss_Status_BeingExecuted);
	ExecutionControlTags.AddTag(MyGameplayTags::Boss_Status_Executed);

}

void UBTService_UpdateBossState::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController)
	{
		return;
	}

	ABossCharacter* Boss = Cast<ABossCharacter>(AIController->GetPawn());
	if (!Boss || !Boss->GetFirstAbilitySystemComponent())
	{
		return;
	}

	const bool bIsDead = Boss->GetFirstAbilitySystemComponent()->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead);

	BB->SetValueAsBool(TEXT("bIsDead"), bIsDead);

	// 忙碌状态：只要 ActionBlockingTags 里有任意标签存在，就挡住追击/巡逻。
	// 单一黑板键 bIsBusy，行为树不需要为每个状态单独加键和装饰器。
	const bool bIsBusy = Boss->GetFirstAbilitySystemComponent()
		->HasAnyMatchingGameplayTags(ActionBlockingTags);
	BB->SetValueAsBool(TEXT("bIsBusy"), bIsBusy);
	const bool bProvokedByDamage =
		BB->GetValueAsBool(TEXT("bProvokedByDamage"));
	const bool bAttackDirectionLocked =
		Boss->GetFirstAbilitySystemComponent()->HasMatchingGameplayTag(
			MyGameplayTags::Boss_Status_AttackDirectionLocked);
	const bool bExecutionControlled =
		Boss->GetFirstAbilitySystemComponent()->HasAnyMatchingGameplayTags(
			ExecutionControlTags);

	// 统一选择旋转模式：死亡/处决接管强制冻结；普通忙碌仍面向目标。
	AActor* Target = Cast<AActor>(
		BB->GetValueAsObject(TEXT("TargetActor")));
	ABossAIController* BossController =
		Cast<ABossAIController>(AIController);
	const bool bTargetNavigationBlocked =
		BossController && BossController->IsTargetNavigationBlocked(Target);
	const bool bShouldChase =
		BB->GetValueAsBool(TEXT("bShouldChase"));
	const bool bPlayerTooFar =
		BB->GetValueAsBool(TEXT("bPlayerTooFar"));

	EBossRotationMode DesiredRotationMode =
		EBossRotationMode::OrientToMovement;

	if (bIsDead)
	{
		DesiredRotationMode = EBossRotationMode::Frozen;
		AIController->StopMovement();
	}
	else if (bExecutionControlled)
	{
		// 破韧定格 / 处决 Target 蒙太奇 / 非致死起立期间：旋转完全冻结，
		// 面向表现由动画与处决流程负责，AI 不得用 Focus 持续扭向玩家。
		// 这里同时终止残留寻路，避免处决中继续滑行。
		DesiredRotationMode = EBossRotationMode::Frozen;
		AIController->StopMovement();
	}
	else if (bTargetNavigationBlocked)
	{
		// 不可达目标只保留作重新检测；巡逻移动与朝向必须完全接管。
		DesiredRotationMode = EBossRotationMode::OrientToMovement;
	}
	else if (bProvokedByDamage)
	{
		DesiredRotationMode = EBossRotationMode::Frozen;

		// Frozen 只管旋转；这里再终止残留寻路，避免任务手动转身时继续滑行。
		AIController->StopMovement();
	}
	else if (bAttackDirectionLocked)
	{
		// 技能已经提交攻击方向：停止 AI Focus 抢占 Yaw，交给 Montage / Motion Warping。
		DesiredRotationMode = EBossRotationMode::Frozen;
		AIController->StopMovement();
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
	}
	else if (bIsBusy)
	{
		// 有可达目标时保留 Montage 的面向能力；目标不可达时冻结自动转向，避免朝巡逻外目标扭头。
		DesiredRotationMode = IsValid(Target) && !bTargetNavigationBlocked
			? EBossRotationMode::FaceTarget
			: EBossRotationMode::Frozen;
		AIController->StopMovement();
	}
	else if (IsValid(Target) &&
		!bTargetNavigationBlocked &&
		bShouldChase &&
		!bPlayerTooFar)
	{
		DesiredRotationMode = EBossRotationMode::FaceTarget;
	}

	Boss->SetBossRotationMode(DesiredRotationMode);

	if (bExecutionControlled ||
		bAttackDirectionLocked ||
		bTargetNavigationBlocked ||
		DesiredRotationMode == EBossRotationMode::OrientToMovement ||
		!IsValid(Target))
	{
		// 巡逻/远追时移动方向拥有旋转权，不能保留 Gameplay Focus。
		// 警戒、近战与受袭任务会在真正需要时重新取得 Focus。
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
	}
	else if (DesiredRotationMode == EBossRotationMode::FaceTarget &&
		AIController->GetFocusActor() != Target)
	{
		AIController->SetFocus(
			Target,
			EAIFocusPriority::Gameplay);
	}

	if (bIsDead)
	{
		// 死亡后立刻停止移动、停止面向目标、清除目标，避免死后还追着玩家。
		AIController->StopMovement();
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
		BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
		BB->SetValueAsBool(TEXT("bInAlertRange"), false);
		BB->SetValueAsBool(TEXT("bInAttackRange"), false);
		BB->SetValueAsBool(TEXT("bShouldChase"), false);
		BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
		if (BossController)
		{
			BossController->SetTargetNavigationBlocked(Target, false);
		}
	}
}
