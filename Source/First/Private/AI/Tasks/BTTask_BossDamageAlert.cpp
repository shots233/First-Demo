#include "AI/Tasks/BTTask_BossDamageAlert.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "Controller/BossAIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"

namespace
{
	struct FBossDamageAlertTaskMemory
	{
		float ElapsedTime = 0.f;
		float StartYaw = 0.f;
		float TargetYawUnwrapped = 0.f;
		float DrawSwordElapsedTime = 0.f;
		bool bTurnCompleted = false;
		bool bCombatStartSent = false;
		bool bWasTurnPaused = false;
		// 只有通过 ExecuteTask 前置条件后，任务才拥有停止移动、Focus 与
		// 旋转状态的清理权。行为树搜索到未触发的分支时不能反复清理战斗状态。
		bool bOwnsAlertTransaction = false;
	};

	bool IsBossDead(const UAbilitySystemComponent* BossASC)
	{
		return !BossASC ||
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Shared_Status_Dead);
	}

	bool IsTargetDead(AActor* Target)
	{
		UAbilitySystemComponent* TargetASC = IsValid(Target)
			? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target)
			: nullptr;

		return TargetASC &&
			TargetASC->HasMatchingGameplayTag(
				MyGameplayTags::Shared_Status_Dead);
	}

	bool IsBossInTerminalControl(const UAbilitySystemComponent* BossASC)
	{
		return !BossASC ||
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Shared_Status_Dead) ||
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Boss_Status_Executable) ||
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Boss_Status_BeingExecuted) ||
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Boss_Status_Executed);
	}

	void InitializeTurnFromCurrentPose(
		FBossDamageAlertTaskMemory& Memory,
		const ABossCharacter& Boss,
		const AActor& Target)
	{
		Memory.ElapsedTime = 0.f;
		Memory.StartYaw = Boss.GetActorRotation().Yaw;

		FVector ToTarget = Target.GetActorLocation() - Boss.GetActorLocation();
		ToTarget.Z = 0.f;

		const float DesiredYaw = ToTarget.IsNearlyZero()
			? Memory.StartYaw
			: ToTarget.Rotation().Yaw;

		Memory.TargetYawUnwrapped =
			Memory.StartYaw +
			FMath::FindDeltaAngleDegrees(Memory.StartYaw, DesiredYaw);
		Memory.bWasTurnPaused = false;
	}

	// 失败与 Behavior Tree Abort 共用这一条幂等清理路径。
	void CleanupFailedOrAbortedDamageAlert(
		ABossAIController* AIController,
		UBlackboardComponent* BB,
		ABossCharacter* Boss)
	{
		const bool bDeferredLossResolved =
			AIController && AIController->ResolveDeferredPerceptionLoss();

		UAbilitySystemComponent* BossASC =
			Boss ? Boss->GetAbilitySystemComponent() : nullptr;
		AActor* Target = BB
			? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
			: nullptr;

		const bool bCanRetainTarget =
			!bDeferredLossResolved &&
			BB &&
			IsValid(Target) &&
			!IsTargetDead(Target) &&
			!IsBossInTerminalControl(BossASC);
		const bool bTargetNavigationBlocked =
			AIController &&
			IsValid(Target) &&
			AIController->IsTargetNavigationBlocked(Target);
		const bool bCanFaceTarget =
			bCanRetainTarget &&
			AIController &&
			!bTargetNavigationBlocked;
		const bool bWeaponDrawn = BossASC &&
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Boss_Status_WeaponDrawn);
		const bool bCanResumeCombat = bCanFaceTarget && bWeaponDrawn;

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[BossAITrace] Damage alert cleanup | "
				 "DeferredLoss=%s | RetainTarget=%s | WeaponDrawn=%s | "
				 "NavBlocked=%s | ResumeCombat=%s"),
			bDeferredLossResolved ? TEXT("true") : TEXT("false"),
			bCanRetainTarget ? TEXT("true") : TEXT("false"),
			bWeaponDrawn ? TEXT("true") : TEXT("false"),
			bTargetNavigationBlocked ? TEXT("true") : TEXT("false"),
			bCanResumeCombat ? TEXT("true") : TEXT("false"));

		if (BB)
		{
			// 拔剑已经完成时，即使任务被 Behavior Tree 中止，也必须把控制权
			// 直接交给战斗分支，不能回落到普通 Boss Alert 再等待一次。
			BB->SetValueAsBool(TEXT("bShouldChase"), bCanResumeCombat);
			if (bCanResumeCombat)
			{
				BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
			}
			// 观察键最后释放，确保行为树重新搜索时已经看到完整的后继状态。
			BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);

			if (!bCanRetainTarget)
			{
				BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
				BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
				BB->SetValueAsBool(TEXT("bInAlertRange"), false);
				BB->SetValueAsBool(TEXT("bInAttackRange"), false);
				BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
				BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
				BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
				BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
				BB->SetValueAsBool(TEXT("bHasAttacked"), false);
			}
		}

		if (AIController)
		{
			AIController->StopMovement();
			if (bCanFaceTarget)
			{
				AIController->SetFocus(Target, EAIFocusPriority::Gameplay);
			}
			else
			{
				AIController->ClearFocus(EAIFocusPriority::Gameplay);
			}
		}

		if (Boss)
		{
			const EBossRotationMode RotationMode =
				IsBossInTerminalControl(BossASC)
				? EBossRotationMode::Frozen
				: (bCanFaceTarget
					? EBossRotationMode::FaceTarget
					: EBossRotationMode::OrientToMovement);
			Boss->SetBossRotationMode(RotationMode);
		}
	}
}

UBTTask_BossDamageAlert::UBTTask_BossDamageAlert()
{
	NodeName = TEXT("Boss Damage Alert");
	INIT_TASK_NODE_NOTIFY_FLAGS();
}

uint16 UBTTask_BossDamageAlert::GetInstanceMemorySize() const
{
	return sizeof(FBossDamageAlertTaskMemory);
}

EBTNodeResult::Type UBTTask_BossDamageAlert::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	FBossDamageAlertTaskMemory* Memory =
		reinterpret_cast<FBossDamageAlertTaskMemory*>(NodeMemory);
	Memory->bOwnsAlertTransaction = false;

	ABossAIController* AIController =
		Cast<ABossAIController>(OwnerComp.GetAIOwner());
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	ABossCharacter* Boss = AIController
		? AIController->GetBossCharacter()
		: nullptr;
	AActor* Target = BB
		? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
		: nullptr;
	UAbilitySystemComponent* BossASC =
		Boss ? Boss->GetAbilitySystemComponent() : nullptr;

	// Behavior Tree 可以在装饰器重新搜索期间访问到这个任务。没有受伤触发时
	// 只是普通的“分支不匹配”，任务尚未取得任何状态所有权，不能调用清理函数。
	if (!AIController || !BB || !Boss ||
		!BB->GetValueAsBool(TEXT("bProvokedByDamage")))
	{
		return EBTNodeResult::Failed;
	}

	if (!IsValid(Target) ||
		IsBossDead(BossASC) ||
		IsTargetDead(Target) ||
		AIController->IsTargetNavigationBlocked(Target))
	{
		CleanupFailedOrAbortedDamageAlert(AIController, BB, Boss);
		return EBTNodeResult::Failed;
	}

	Memory->bOwnsAlertTransaction = true;
	Memory->bTurnCompleted = false;
	Memory->bCombatStartSent = false;
	Memory->bWasTurnPaused = false;
	Memory->DrawSwordElapsedTime = 0.f;
	InitializeTurnFromCurrentPose(*Memory, *Boss, *Target);

	AIController->StopMovement();
	if (UCharacterMovementComponent* Movement = Boss->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	AIController->SetFocus(Target, EAIFocusPriority::Gameplay);
	Boss->SetBossRotationMode(EBossRotationMode::Frozen);
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_BossDamageAlert::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	FBossDamageAlertTaskMemory* Memory =
		reinterpret_cast<FBossDamageAlertTaskMemory*>(NodeMemory);
	if (!Memory->bOwnsAlertTransaction)
	{
		return EBTNodeResult::Aborted;
	}
	Memory->bOwnsAlertTransaction = false;

	ABossAIController* AIController =
		Cast<ABossAIController>(OwnerComp.GetAIOwner());
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	ABossCharacter* Boss = AIController
		? AIController->GetBossCharacter()
		: nullptr;

	UE_LOG(
		LogTemp,
		Display,
		TEXT("[BossAITrace] Damage alert aborted by behavior tree"));
	CleanupFailedOrAbortedDamageAlert(AIController, BB, Boss);
	return EBTNodeResult::Aborted;
}

void UBTTask_BossDamageAlert::TickTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	FBossDamageAlertTaskMemory* Memory =
		reinterpret_cast<FBossDamageAlertTaskMemory*>(NodeMemory);

	ABossAIController* AIController =
		Cast<ABossAIController>(OwnerComp.GetAIOwner());
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	ABossCharacter* Boss = AIController
		? AIController->GetBossCharacter()
		: nullptr;
	AActor* Target = BB
		? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
		: nullptr;
	UAbilitySystemComponent* BossASC =
		Boss ? Boss->GetAbilitySystemComponent() : nullptr;

	if (!AIController ||
		!BB ||
		!Boss ||
		!IsValid(Target) ||
		IsBossDead(BossASC) ||
		IsTargetDead(Target) ||
		AIController->IsTargetNavigationBlocked(Target) ||
		!BB->GetValueAsBool(TEXT("bProvokedByDamage")))
	{
		if (Memory->bOwnsAlertTransaction)
		{
			Memory->bOwnsAlertTransaction = false;
			CleanupFailedOrAbortedDamageAlert(AIController, BB, Boss);
		}
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// 处决事务接管行为；不能让最高优先级受袭任务无限等待。
	const bool bExecutionControlled =
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed);
	if (bExecutionControlled)
	{
		Memory->bOwnsAlertTransaction = false;
		CleanupFailedOrAbortedDamageAlert(AIController, BB, Boss);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const bool bDrawSwordActive =
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_DrawSword);
	if (bDrawSwordActive)
	{
		Memory->DrawSwordElapsedTime += FMath::Max(DeltaSeconds, 0.f);
		if (DrawSwordTimeout > KINDA_SMALL_NUMBER &&
			Memory->DrawSwordElapsedTime >= DrawSwordTimeout)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[Boss Damage Alert] 等待拔剑完成超过 %.2f 秒，"
					 "请检查拔剑 Montage 是否循环以及 DrawSword 标签是否正常移除。"),
				DrawSwordTimeout);
			Memory->bOwnsAlertTransaction = false;
			CleanupFailedOrAbortedDamageAlert(AIController, BB, Boss);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}
	}
	else
	{
		Memory->DrawSwordElapsedTime = 0.f;
	}

	// 短暂硬直只暂停；恢复后必须从 Montage 改变后的当前朝向重新计时。
	if (BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Staggered))
	{
		if (!Memory->bTurnCompleted)
		{
			Memory->bWasTurnPaused = true;
		}
		return;
	}

	if (!Memory->bTurnCompleted &&
		(BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Attacking) ||
		 bDrawSwordActive))
	{
		Memory->bWasTurnPaused = true;
		return;
	}

	if (!Memory->bTurnCompleted && Memory->bWasTurnPaused)
	{
		InitializeTurnFromCurrentPose(*Memory, *Boss, *Target);
	}

	if (!Memory->bTurnCompleted)
	{
		Memory->ElapsedTime += FMath::Max(DeltaSeconds, 0.f);

		FVector ToTarget = Target->GetActorLocation() - Boss->GetActorLocation();
		ToTarget.Z = 0.f;
		const float DesiredYaw = ToTarget.IsNearlyZero()
			? Boss->GetActorRotation().Yaw
			: ToTarget.Rotation().Yaw;

		Memory->TargetYawUnwrapped +=
			FMath::FindDeltaAngleDegrees(
				FRotator::NormalizeAxis(Memory->TargetYawUnwrapped),
				DesiredYaw);

		const float RawAlpha = TurnDuration <= KINDA_SMALL_NUMBER
			? 1.f
			: FMath::Clamp(Memory->ElapsedTime / TurnDuration, 0.f, 1.f);
		const float SmoothAlpha =
			RawAlpha * RawAlpha * (3.f - 2.f * RawAlpha);
		const float NewYaw = FRotator::NormalizeAxis(
			Memory->StartYaw +
			(Memory->TargetYawUnwrapped - Memory->StartYaw) * SmoothAlpha);

		Boss->SetActorRotation(FRotator(0.f, NewYaw, 0.f));

		const float RemainingYaw = FMath::Abs(
			FMath::FindDeltaAngleDegrees(
				Boss->GetActorRotation().Yaw,
				DesiredYaw));
		if (RawAlpha < 1.f && RemainingYaw > TurnToleranceDegrees)
		{
			return;
		}

		Boss->SetActorRotation(FRotator(0.f, DesiredYaw, 0.f));
		AIController->SetFocus(Target, EAIFocusPriority::Gameplay);
		Memory->bTurnCompleted = true;

		BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
		BB->SetValueAsBool(TEXT("bShouldChase"), true);

		const bool bWeaponAlreadyDrawn =
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Boss_Status_WeaponDrawn);
		const bool bDrawSwordAlreadyRunning =
			BossASC->HasMatchingGameplayTag(
				MyGameplayTags::Boss_Status_DrawSword);

		if (!bWeaponAlreadyDrawn && !bDrawSwordAlreadyRunning)
		{
			Memory->bCombatStartSent = true;

			FGameplayEventData EventData;
			EventData.EventTag = MyGameplayTags::Boss_Event_CombatStart;
			EventData.Instigator = Target;
			EventData.Target = Boss;

			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
				Boss,
				MyGameplayTags::Boss_Event_CombatStart,
				EventData);
		}
	}

	const bool bDrawingNow =
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_DrawSword);
	const bool bWeaponDrawnNow =
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_WeaponDrawn);

	if (bDrawingNow)
	{
		return;
	}

	if (bWeaponDrawnNow)
	{
		if (AIController->ResolveDeferredPerceptionLoss())
		{
			Memory->bOwnsAlertTransaction = false;
			CleanupFailedOrAbortedDamageAlert(AIController, BB, Boss);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}

		// 原子交接顺序：先确认战斗键，再释放最高优先级受袭键。
		// 即使 bShouldChase 在拔剑期间被其它清理路径改过，也不会掉回普通警戒。
		BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
		BB->SetValueAsBool(TEXT("bShouldChase"), true);
		// 先释放事务所有权，再修改观察键。若该写入触发同步 Abort，
		// AbortTask 不会把刚建立的战斗状态再次清掉。
		Memory->bOwnsAlertTransaction = false;
		BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
		Boss->SetBossRotationMode(EBossRotationMode::FaceTarget);
		AIController->SetFocus(Target, EAIFocusPriority::Gameplay);
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[BossAITrace] Damage alert handed directly to combat | Target=%s"),
			*GetNameSafe(Target));
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	if (Memory->bCombatStartSent)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[Boss Damage Alert] CombatStart 后未进入 DrawSword，"
				 "请检查 GA_Boss_DrawSword 是否已授予以及触发标签。"));
	}
	else
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[Boss Damage Alert] BOSS 既未拔剑也不在拔剑中。"));
	}

	Memory->bOwnsAlertTransaction = false;
	CleanupFailedOrAbortedDamageAlert(AIController, BB, Boss);
	FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
}
