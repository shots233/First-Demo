// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/UBTService_UpdateCombatDistance.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BaseCharacter.h"
#include "Controller/BossAIController.h"
#include "MyGameplayTags.h"

UBTService_UpdateCombatDistance::UBTService_UpdateCombatDistance()
{
	NodeName = TEXT("Update Combat Distance");

}

void UBTService_UpdateCombatDistance::TickNode(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController)
	{
		return;
	}

	const APawn* BossPawn = AIController->GetPawn();
	ABossAIController* BossController = Cast<ABossAIController>(AIController);
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")));

	if (!IsValid(BossPawn) || !IsValid(Target))
	{
		// 只有从战斗状态掉到“无目标”的边沿才停止旧寻路。
		// 稳定巡逻时 TargetActor 本来就是空；若每个 Service Tick 都 StopMovement，
		// 当前巡逻分支刚启动的 Move To 会被根节点服务反复中止。
		const bool bHadCombatState =
			BB->GetValueAsObject(TEXT("TargetActor")) != nullptr ||
			BB->GetValueAsBool(TEXT("bPlayerDetected")) ||
			BB->GetValueAsBool(TEXT("bInAlertRange")) ||
			BB->GetValueAsBool(TEXT("bInAttackRange")) ||
			BB->GetValueAsBool(TEXT("bPlayerTooClose")) ||
			BB->GetValueAsBool(TEXT("bInStrafeRange")) ||
			BB->GetValueAsBool(TEXT("bPlayerTooFar")) ||
			BB->GetValueAsBool(TEXT("bShouldChase")) ||
			BB->GetValueAsBool(TEXT("bProvokedByDamage")) ||
			AIController->GetFocusActor() != nullptr;
		if (bHadCombatState)
		{
			AIController->StopMovement();
		}
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
		BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
		BB->SetValueAsBool(TEXT("bInAlertRange"), false);
		BB->SetValueAsBool(TEXT("bInAttackRange"), false);
		BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
		BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
		BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
		BB->SetValueAsBool(TEXT("bShouldChase"), false);
		BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
		BB->SetValueAsBool(TEXT("bHasAttacked"), false);
		BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
		if (BossController)
		{
			BossController->SetTargetNavigationBlocked(nullptr, false);
		}
		return;
	}

	// 【主角死亡】目标已死亡时：取消攻击、清目标与全部战斗标志，回巡逻。
	UAbilitySystemComponent* TargetASC =
		UAbilitySystemBlueprintLibrary::
		GetAbilitySystemComponent(Target);

	const bool bTargetDead = TargetASC &&
		TargetASC->HasMatchingGameplayTag(
			MyGameplayTags::Shared_Status_Dead);

	if (bTargetDead)
	{
		// 取消正在播放的 BOSS 攻击；不取消死亡、受击等其它能力。
		if (const ABaseCharacter* BossCharacter =
			Cast<ABaseCharacter>(BossPawn))
		{
			if (UFirstAbilitySystemComponent* BossASC =
				BossCharacter->GetFirstAbilitySystemComponent())
			{
				FGameplayTagContainer AttackTags;
				AttackTags.AddTag(
					MyGameplayTags::Boss_Ability_Attack);
				BossASC->CancelAbilities(&AttackTags);
			}
		}

		AIController->StopMovement();
		AIController->ClearFocus(EAIFocusPriority::Gameplay);

		BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
		BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
		BB->SetValueAsBool(TEXT("bInAlertRange"), false);
		BB->SetValueAsBool(TEXT("bInAttackRange"), false);
		BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
		BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
		BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
		BB->SetValueAsBool(TEXT("bShouldChase"), false);
		BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
		BB->SetValueAsBool(TEXT("bHasAttacked"), false);
		BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
		if (BossController)
		{
			BossController->SetTargetNavigationBlocked(Target, false);
		}
		return;
	}

	// 目标不在 NavMesh 上时保留 TargetActor 供后续复查，但禁止所有战斗注视与追击。
	// 控制器统一执行精确投射距离与迟滞判定，伤害/感知回调也使用同一事实来源。
	const bool bWasNavigationBlocked = BossController &&
		BossController->IsTargetNavigationBlocked(Target);
	const bool bTargetNavigationBlocked = !BossController ||
		BossController->RefreshTargetNavigationBlocked(Target);

	if (bTargetNavigationBlocked)
	{
		if (BossController)
		{
			BossController->SetTargetNavigationBlocked(Target, true);
		}

		// 只在刚离开 NavMesh 的边沿取消旧追击/攻击；后续 Tick 不能反复 StopMovement，
		// 否则已经回到巡逻分支的 Move To 永远走不起来。
		if (!bWasNavigationBlocked)
		{
			if (const ABaseCharacter* BossCharacter =
				Cast<ABaseCharacter>(BossPawn))
			{
				if (UFirstAbilitySystemComponent* BossASC =
					BossCharacter->GetFirstAbilitySystemComponent())
				{
					FGameplayTagContainer AttackTags;
					AttackTags.AddTag(MyGameplayTags::Boss_Ability_Attack);
					BossASC->CancelAbilities(&AttackTags);
				}
			}

			AIController->StopMovement();
		}
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
		BB->SetValueAsBool(TEXT("bInAlertRange"), false);
		BB->SetValueAsBool(TEXT("bInAttackRange"), false);
		BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
		BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
		BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
		BB->SetValueAsBool(TEXT("bShouldChase"), false);
		BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
		BB->SetValueAsBool(TEXT("bHasAttacked"), false);
		BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
		return;
	}

	BB->SetValueAsBool(TEXT("bPlayerDetected"), true);

	// WeaponDrawn 是“已经完成入战仪式”的持久事实。只要目标重新可达，
	// 已拔剑的 BOSS 就直接恢复战斗，不能再次进入普通 3 秒警戒。
	const ABaseCharacter* BossCharacter = Cast<ABaseCharacter>(BossPawn);
	const UFirstAbilitySystemComponent* BossASC = BossCharacter
		? BossCharacter->GetFirstAbilitySystemComponent()
		: nullptr;
	if (BossASC && BossASC->HasMatchingGameplayTag(
		MyGameplayTags::Boss_Status_WeaponDrawn))
	{
		BB->SetValueAsBool(TEXT("bShouldChase"), true);
	}

	const float Distance = FVector::Dist(
		BossPawn->GetActorLocation(),
		Target->GetActorLocation());

	BB->SetValueAsBool(TEXT("bInAlertRange"), Distance <= AlertRadius);
	BB->SetValueAsBool(TEXT("bInAttackRange"), Distance <= AttackRadius);

	// 三距离对峙（D27）：最小=AttackRadius，周旋=StrafeRadius，最大=AlertRadius。
	BB->SetValueAsBool(TEXT("bPlayerTooClose"), Distance < AttackRadius);
	BB->SetValueAsBool(TEXT("bInStrafeRange"), Distance >= AttackRadius && Distance <= StrafeRadius);
	BB->SetValueAsBool(TEXT("bPlayerTooFar"), Distance > StrafeRadius);
}
