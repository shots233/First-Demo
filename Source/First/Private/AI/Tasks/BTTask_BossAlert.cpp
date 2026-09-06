// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/Tasks/BTTask_BossAlert.h"

#include "AIController.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "Controller/BossAIController.h"
#include "GameFramework/Pawn.h"
#include "MyGameplayTags.h"

// 每个任务实例自己持有的临时数据，放在任务内存里，而不是类的成员。
struct FBossAlertTaskMemory
{
	float ElapsedTime = 0.f;
};

UBTTask_BossAlert::UBTTask_BossAlert()
{
	NodeName = TEXT("Boss Alert");

	// 必须打开任务节点的 Tick 通知，TickTask 才会被调用。
	// 缺少这行：任务会一直停在 InProgress，永远不结束。
	INIT_TASK_NODE_NOTIFY_FLAGS();
}

uint16 UBTTask_BossAlert::GetInstanceMemorySize() const
{
	return sizeof(FBossAlertTaskMemory);
}


EBTNodeResult::Type UBTTask_BossAlert::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AActor* Target = BB
		? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
		: nullptr;
	ABossAIController* BossController = Cast<ABossAIController>(AIController);

	if (!AIController ||
		!BB ||
		!IsValid(Target) ||
		(BossController && BossController->IsTargetNavigationBlocked(Target)))
	{
		if (AIController)
		{
			AIController->ClearFocus(EAIFocusPriority::Gameplay);
		}
		return EBTNodeResult::Failed;
	}

	ABossCharacter* Boss = Cast<ABossCharacter>(AIController->GetPawn());
	UAbilitySystemComponent* BossASC = Boss
		? Boss->GetAbilitySystemComponent()
		: nullptr;

	// 死亡或处决事务接管期间不得进入对峙：不写黑板、不抢 Focus，
	// 朝向由 UpdateBossState 的 Frozen 分支全权管理（口径同 BTTask_BossDamageAlert）。
	if (!Boss || !BossASC ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed))
	{
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		return EBTNodeResult::Failed;
	}

	if (BossASC && BossASC->HasMatchingGameplayTag(
		MyGameplayTags::Boss_Status_WeaponDrawn))
	{
		// 安全网：若其它状态在同一帧短暂把 bShouldChase 清掉，已经拔剑的
		// BOSS 也只会瞬时通过该节点，不会再次播放完整警戒等待。
		BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
		BB->SetValueAsBool(TEXT("bShouldChase"), true);
		AIController->SetFocus(Target, EAIFocusPriority::Gameplay);
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[BossAITrace] Alert skipped because weapon is already drawn | Target=%s"),
			*GetNameSafe(Target));
		return EBTNodeResult::Succeeded;
	}

	FBossAlertTaskMemory* Memory =
		reinterpret_cast<FBossAlertTaskMemory*>(NodeMemory);
	Memory->ElapsedTime = 0.f;

	// 让 AIController 的 Focus 指向玩家（为以后追击中的转向打基础）。
	AIController->SetFocus(Target, EAIFocusPriority::Gameplay);

	// 对峙需要时间，返回 InProgress，由 TickTask 决定何时结束。
	return EBTNodeResult::InProgress;

}

void UBTTask_BossAlert::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();

	if (!AIController || !BB)
	{
		// 【UE 5.6 修正】FinishLatentTask 只需要两个参数，不再传 NodeMemory。
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")));
	ABossAIController* BossController = Cast<ABossAIController>(AIController);

	// 死亡或处决接管（破韧定格 / 被处决 / 起立）时立即退出对峙：
	// 停掉下面的每帧 SetActorRotation 硬转身，避免与处决蒙太奇抢朝向。
	ABossCharacter* Boss = Cast<ABossCharacter>(AIController->GetPawn());
	UAbilitySystemComponent* BossASC = Boss
		? Boss->GetAbilitySystemComponent()
		: nullptr;
	if (!Boss || !BossASC ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed))
	{
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		// 【UE 5.6 修正】两参数版本，不要带 NodeMemory。
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// 对峙中断条件：目标丢失，或玩家离开了警戒范围。
	if (!IsValid(Target) ||
		!BB->GetValueAsBool(TEXT("bInAlertRange")) ||
		(BossController && BossController->IsTargetNavigationBlocked(Target)))
	{
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		// 【UE 5.6 修正】两参数版本，不要带 NodeMemory。
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	FBossAlertTaskMemory* Memory =
		reinterpret_cast<FBossAlertTaskMemory*>(NodeMemory);
	Memory->ElapsedTime += DeltaSeconds;

	// 面向玩家：只旋转 Yaw，保持站立姿态。
	// 注意：ACharacter 默认由移动方向决定朝向（bOrientRotationToMovement），
	// 站立不动时不会自动转向玩家，所以这里显式旋转。
	if (bRotateToFaceTarget)
	{
		if (APawn* Pawn = AIController->GetPawn())
		{
			const FVector ToTarget =
				Target->GetActorLocation() - Pawn->GetActorLocation();
			const FRotator TargetRotation = ToTarget.Rotation();
			Pawn->SetActorRotation(FRotator(0.f, TargetRotation.Yaw, 0.f));
		}
	}

	if (Memory->ElapsedTime >= StareDuration)
	{
		// 对峙结束：允许追击，并触发 BOSS 拔剑（CombatStart）。
		BB->SetValueAsBool(TEXT("bShouldChase"), true);

		// 拔剑时机：对峙结束才发，而不是"看见就拔"。
		// Boss 已在 TickTask 顶部的中断检查里保证非空，直接复用。
		if (Boss)
		{
			FGameplayEventData EventData;
			EventData.Instigator = Target;
			EventData.Target = Target;
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
				Boss,
				MyGameplayTags::Boss_Event_CombatStart,
				EventData);
		}

		// 【UE 5.6 修正】两参数版本，不要带 NodeMemory。
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}
