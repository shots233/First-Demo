#include "AbilitySystem/Abilities/DK/FirstGA_DKExecute.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Character/BossCharacter.h"
#include "Character/DKCharacter.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"

UFirstGA_DKExecute::UFirstGA_DKExecute()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Action);
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Execute);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_Executing);
	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_Invincible);

	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Attacking);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Defending);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Executing);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
}

ABossCharacter* UFirstGA_DKExecute::FindExecutableBoss(
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!Avatar || !World)
	{
		return nullptr;
	}

	ABossCharacter* BestBoss = nullptr;
	float BestDistanceSquared = FMath::Square(ExecutionRange);

	for (TActorIterator<ABossCharacter> It(World); It; ++It)
	{
		ABossCharacter* Candidate = *It;
		if (!IsValid(Candidate))
		{
			continue;
		}

		UAbilitySystemComponent* BossASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Candidate);
		if (!BossASC ||
			BossASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead) ||
			!BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable) ||
			BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(Avatar->GetActorLocation(),Candidate->GetActorLocation());

		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestBoss = Candidate;
		}
	}

	return BestBoss;
}

bool UFirstGA_DKExecute::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle,ActorInfo,SourceTags,TargetTags,OptionalRelevantTags))
	{
		return false;
	}

	const ADKCharacter* DK = ActorInfo
		? Cast<ADKCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;

	return DK &&DK->GetCharacterMovement()->IsMovingOnGround() &&FindExecutableBoss(ActorInfo) != nullptr;
}

void UFirstGA_DKExecute::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	ABossCharacter* Boss = FindExecutableBoss(ActorInfo);

	// 在改变位置/移动前先验证双方动画，避免缺资产时把角色锁死。
	if (!DK || !Boss ||
		!DK->GetExecutionMontage() ||
		!Boss->GetExecutionTargetMontage())
	{
		FinishExecution(true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishExecution(true);
		return;
	}

	// CanActivate 检查和移动 Tick 之间可能仍残留同帧 Jump 请求。
	DK->StopJumping();

	TargetBoss = Boss;
	bTerminalEventSent = false;

	// BOSS Ability 若被外部死亡/强制取消，主动结束玩家侧锁移动和无敌。
	UAbilityTask_WaitGameplayEvent* BossAbortTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_ExecutionAbortedByBoss,
			nullptr,
			false,
			true);
	BossAbortTask->EventReceived.AddDynamic(this,&ThisClass::HandleBossExecutionAborted);
	BossAbortTask->ReadyForActivation();

	// 先用玩家原始站位请求 BOSS 接受处决；BOSS 只有在 Target Montage
	// 真正开始播放后才会同步添加 BeingExecuted 作为确认。
	SendBossEvent(MyGameplayTags::Boss_Event_ExecutionStarted);

	UAbilitySystemComponent* BossASC =UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Boss);
	if (!BossASC || !BossASC->HasMatchingGameplayTag(
		MyGameplayTags::Boss_Status_BeingExecuted))
	{
		// 请求被拒绝时尚未改位置、移动或碰撞，直接结束不会产生免费瞬移。
		// BOSS 从未确认启动，不需要再回发一条 ExecutionAborted。
		bTerminalEventSent = true;
		FinishExecution(true);
		return;
	}

	if (UCharacterMovementComponent* Movement = DK->GetCharacterMovement())
	{
		SavedMovementMode = Movement->MovementMode;
		SavedCustomMovementMode = Movement->CustomMovementMode;
		bSavedMovementMode = true;

		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	UCapsuleComponent* PlayerCapsule = DK->GetCapsuleComponent();
	UCapsuleComponent* BossCapsule = Boss->GetCapsuleComponent();
	if (PlayerCapsule)
	{
		SavedPlayerPawnResponse =PlayerCapsule->GetCollisionResponseToChannel(ECC_Pawn);
		bSavedPlayerPawnResponse = true;
		PlayerCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	if (BossCapsule)
	{
		SavedBossPawnResponse =BossCapsule->GetCollisionResponseToChannel(ECC_Pawn);
		bSavedBossPawnResponse = true;
		BossCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	const FTransform ExecutionTransform = Boss->GetExecutionPointTransform();
	DK->SetActorLocationAndRotation(
		ExecutionTransform.GetLocation(),
		ExecutionTransform.Rotator(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("DKExecutionMontage"),
			DK->GetExecutionMontage());

	MontageTask->OnCompleted.AddDynamic(this,&ThisClass::HandleExecutionCompleted);
	MontageTask->OnInterrupted.AddDynamic(this,&ThisClass::HandleExecutionInterrupted);
	MontageTask->OnCancelled.AddDynamic(this,&ThisClass::HandleExecutionInterrupted);
	MontageTask->ReadyForActivation();
}

void UFirstGA_DKExecute::SendBossEvent(const FGameplayTag& EventTag)
{
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	ABossCharacter* Boss = TargetBoss.Get();
	if (!DK || !Boss || !EventTag.IsValid())
	{
		return;
	}

	FGameplayEventData EventData;
	EventData.Instigator = DK;
	EventData.Target = Boss;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Boss,EventTag,EventData);
}

void UFirstGA_DKExecute::HandleExecutionCompleted()
{
	SendBossEvent(MyGameplayTags::Boss_Event_ExecutionFinished);
	bTerminalEventSent = true;
	FinishExecution(false);
}

void UFirstGA_DKExecute::HandleExecutionInterrupted()
{
	SendBossEvent(MyGameplayTags::Boss_Event_ExecutionAborted);
	bTerminalEventSent = true;
	FinishExecution(true);
}

void UFirstGA_DKExecute::HandleBossExecutionAborted(FGameplayEventData Payload)
{
	if (!IsActive())
	{
		return;
	}

	// BOSS 已在结束自己的状态；不要再把 Aborted 事件回发给它。
	bTerminalEventSent = true;
	FinishExecution(true);
}

void UFirstGA_DKExecute::FinishExecution(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,bWasCancelled);
}

void UFirstGA_DKExecute::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 外部路径直接取消能力时，也必须通知 BOSS 退出 BeingExecuted。
	if (!bTerminalEventSent && TargetBoss.IsValid())
	{
		SendBossEvent(MyGameplayTags::Boss_Event_ExecutionAborted);
		bTerminalEventSent = true;
	}

	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	ABossCharacter* Boss = TargetBoss.Get();

	if (DK)
	{
		if (UCapsuleComponent* PlayerCapsule = DK->GetCapsuleComponent();PlayerCapsule && bSavedPlayerPawnResponse)
		{
			PlayerCapsule->SetCollisionResponseToChannel(ECC_Pawn,SavedPlayerPawnResponse);
		}

		UAbilitySystemComponent* ASC =UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(DK);
		const bool bPlayerDead = ASC && ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead);

		if (!bPlayerDead && bSavedMovementMode)
		{
			UCharacterMovementComponent* Movement = DK->GetCharacterMovement();
			Movement->MaxWalkSpeed = DK->GetDesiredLocomotionSpeed();
			Movement->SetMovementMode(SavedMovementMode,SavedCustomMovementMode);
		}
	}

	if (Boss)
	{
		UAbilitySystemComponent* BossASC =UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Boss);
		const bool bBossDead = BossASC && BossASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead);

		if (UCapsuleComponent* BossCapsule = Boss->GetCapsuleComponent();
			BossCapsule && bSavedBossPawnResponse)
		{
			if (bBossDead)
			{
				BossCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			}
			else
			{
				BossCapsule->SetCollisionResponseToChannel(ECC_Pawn,SavedBossPawnResponse);
			}
		}
	}

	bSavedMovementMode = false;
	bSavedPlayerPawnResponse = false;
	bSavedBossPawnResponse = false;
	TargetBoss.Reset();

	Super::EndAbility(Handle,ActorInfo,ActivationInfo,bReplicateEndAbility,bWasCancelled);
}