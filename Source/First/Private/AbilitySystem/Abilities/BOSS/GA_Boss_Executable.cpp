#include "AbilitySystem/Abilities/BOSS/GA_Boss_Executable.h"

#include "AIController.h"
#include "Controller/BossAIController.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "AbilitySystem/GameplayEffects/FirstGE_ExecutionDamage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/BossCharacter.h"
#include "Character/DKCharacter.h"
#include "Components/Combat/BossCombatComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"

UGA_Boss_Executable::UGA_Boss_Executable()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Executable);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);

	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::Boss_Event_PoiseBroken;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UGA_Boss_Executable::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =GetFirstAbilitySystemComponentFromActorInfo();
	if (!Boss || !ASC)
	{
		FinishExecutable(true);
		return;
	}

	bExecutionStarted = false;
	bExecutionDamageApplied = false;
	bBossKilledByExecution = false;
	bPlayerTerminalEventHandled = false;
	bOwnsBeingExecutedTag = false;
	bExecutionGetUpStarted = false;
	ExecutingPlayer.Reset();

	// 普通武器可能在拔剑、攻击、普通受击或弹反僵直期间打空 Poise。
	// CancelAbilities 对 WithTags 使用 HasAny，因此命中任一标签都会被取消。
	FGameplayTagContainer AbilitiesToCancel;
	AbilitiesToCancel.AddTag(MyGameplayTags::Boss_Ability_DrawSword);
	AbilitiesToCancel.AddTag(MyGameplayTags::Boss_Ability_Attack);
	AbilitiesToCancel.AddTag(MyGameplayTags::Boss_Ability_ParryStagger);
	AbilitiesToCancel.AddTag(MyGameplayTags::Boss_Ability_HitReact);

	ASC->CancelAbilities(&AbilitiesToCancel, nullptr, this);

	if (UBossCombatComponent* Combat = Boss->GetBossCombatComponent())
	{
		Combat->ToggleWeaponCollision(false);
	}

	StopBossMovement();
	PlayExecutableLoop();

	// false = 不只监听一次。处决启动后若配置错误而中止，玩家可以在剩余窗口重试。
	UAbilityTask_WaitGameplayEvent* StartTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_ExecutionStarted,
			nullptr,
			false,
			true);
	StartTask->EventReceived.AddDynamic(this,&ThisClass::HandleExecutionStarted);
	StartTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* AbortTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_ExecutionAborted,
			nullptr,
			false,
			true);
	AbortTask->EventReceived.AddDynamic(this,&ThisClass::HandleExecutionAborted);
	AbortTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* FinishedTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_ExecutionFinished,
			nullptr,
			false,
			true);
	FinishedTask->EventReceived.AddDynamic(this,&ThisClass::HandleExecutionFinished);
	FinishedTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* DamageTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_ExecutionApplyDamage,
			nullptr,
			false,
			true);
	DamageTask->EventReceived.AddDynamic(this,&ThisClass::HandleExecutionApplyDamage);
	DamageTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* ExpiredTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_ExecutableExpired,
			nullptr,
			false,
			true);
	ExpiredTask->EventReceived.AddDynamic(this,&ThisClass::HandleExecutableExpired);
	ExpiredTask->ReadyForActivation();
}

void UGA_Boss_Executable::StopBossMovement()
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!Boss)
	{
		return;
	}

	Boss->GetCharacterMovement()->StopMovementImmediately();
	if (AAIController* AIController = Cast<AAIController>(Boss->GetController()))
	{
		AIController->StopMovement();
	}
}

void UGA_Boss_Executable::PlayExecutableLoop()
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UAnimMontage* Montage = Boss ? Boss->GetExecutableMontage() : nullptr;
	UAnimInstance* AnimInstance = Boss && Boss->GetMesh()? Boss->GetMesh()->GetAnimInstance(): nullptr;

	if (!AnimInstance || !Montage)
	{
		return;
	}

	AnimInstance->Montage_Play(Montage);
	if (Montage->GetSectionIndex(TEXT("Start")) != INDEX_NONE)
	{
		AnimInstance->Montage_JumpToSection(TEXT("Start"), Montage);
	}
}

void UGA_Boss_Executable::HandleExecutionStarted(FGameplayEventData Payload)
{
	if (bExecutionStarted || bExecutionDamageApplied)
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	ADKCharacter* Player = Cast<ADKCharacter>(const_cast<AActor*>(Payload.Instigator.Get()));
	UFirstAbilitySystemComponent* ASC =GetFirstAbilitySystemComponentFromActorInfo();
	UAnimMontage* TargetMontage = Boss? Boss->GetExecutionTargetMontage(): nullptr;

	if (!Boss || !Player || !ASC || !TargetMontage)
	{
		return;
	}

	// 玩家还没有传送到 ExecutionPoint；这里校验的是请求发生时的原始距离。
	// 它与 ExecutionPoint 的本地 X/Y/Z 偏移无关。
	if (FVector::DistSquared2D(Boss->GetActorLocation(), Player->GetActorLocation()) >
		FMath::Square(FMath::Max(Boss->GetExecutionRequestRange(), 0.f)))
	{
		return;
	}

	UAnimInstance* AnimInstance = Boss->GetMesh()? Boss->GetMesh()->GetAnimInstance(): nullptr;
	if (!AnimInstance)
	{
		return;
	}

	if (UAnimMontage* WeakMontage = Boss->GetExecutableMontage())
	{
		AnimInstance->Montage_Stop(0.1f, WeakMontage);
	}

	const float TargetPlaybackDuration =AnimInstance->Montage_Play(TargetMontage);
	if (TargetPlaybackDuration <= 0.f)
	{
		// 骨骼、Slot 或 Montage 配置错误时不发送确认，并恢复弱点 Loop。
		PlayExecutableLoop();
		return;
	}

	// 只有 Target Montage 真正开始播放后才添加确认 Tag。
	// 玩家 Ability 会在 SendGameplayEventToActor 返回后同步检查它。
	bExecutionStarted = true;
	bPlayerTerminalEventHandled = false;
	ExecutingPlayer = Player;
	Boss->ClearPoiseRecoveryTimer();
	ASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted);
	bOwnsBeingExecutedTag = true;
	StopBossMovement();

	// 玩家 Ability/Actor 异常销毁、终止事件丢失时的最后保险。
	Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	Boss->GetWorldTimerManager().SetTimer(
		ExecutionSafetyTimerHandle,
		this,
		&ThisClass::HandleExecutionSafetyTimeout,
		FMath::Max(TargetPlaybackDuration + 0.5f, 0.5f),
		false);
}

void UGA_Boss_Executable::HandleExecutionAborted(FGameplayEventData Payload)
{
	if (!bExecutionStarted || bExecutionGetUpStarted)
	{
		return;
	}

	bPlayerTerminalEventHandled = true;

	// 伤害前中断：允许回到剩余的可处决窗口。
	if (!bExecutionDamageApplied)
	{
		ReturnToExecutableAfterAbort();
		return;
	}

	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	const bool bBossDead = !ASC || ASC->HasMatchingGameplayTag(
		MyGameplayTags::Shared_Status_Dead);

	// 伤害已经结算且 BOSS 存活：本次处决已经消费，仍然进入起立。
	if (!bBossKilledByExecution && !bBossDead)
	{
		StartExecutionGetUp();
		return;
	}

	FinishExecutable(true);
}

void UGA_Boss_Executable::HandleExecutionFinished(FGameplayEventData Payload)
{
	if (!bExecutionStarted || bExecutionGetUpStarted)
	{
		return;
	}

	// 玩家 Montage 已经完成。玩家可以先恢复控制，BOSS 独自播放起立。
	bPlayerTerminalEventHandled = true;

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();

	if (Boss)
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	}

	if (!bExecutionDamageApplied)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Boss execution finished without Boss.Event.Execution.ApplyDamage"));
		ReturnToExecutableAfterAbort();
		return;
	}

	if (!Boss || !ASC)
	{
		FinishExecutable(true);
		return;
	}

	const bool bBossDead = ASC->HasMatchingGameplayTag(
		MyGameplayTags::Shared_Status_Dead);

	// 致死处决或外部伤害已经把 BOSS 杀死：保留死亡链，绝不起立。
	if (bBossKilledByExecution || bBossDead)
	{
		FinishExecutable(false);
		return;
	}

	StartExecutionGetUp();
}

void UGA_Boss_Executable::StartExecutionGetUp()
{
	if (!IsActive() || bExecutionGetUpStarted)
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();

	if (!Boss || !ASC)
	{
		FinishExecutable(true);
		return;
	}

	if (bBossKilledByExecution ||
		ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead))
	{
		FinishExecutable(false);
		return;
	}

	UAnimMontage* GetUpMontage = Boss->GetExecutionGetUpMontage();
	if (!GetUpMontage)
	{
		// 缺少表现资产时不能永久锁住玩法；记录错误后退回旧版直接站立。
		UE_LOG(LogTemp, Warning,
			TEXT("ExecutionGetUpMontage is not configured on BP_Boss"));
		FinishExecutable(false);
		return;
	}

	StopBossMovement();
	if (UBossCombatComponent* Combat = Boss->GetBossCombatComponent())
	{
		// 非攻击状态的安全默认值始终是关闭，而不是重新打开。
		Combat->ToggleWeaponCollision(false);
	}

	bExecutionGetUpStarted = true;

	// BOSS 存活收尾：主动重建仇恨。起立期间玩家可以先恢复控制跑出视野，
	// 不在这里重新锁定的话，感知丢失会清空战斗目标，起身直接退回巡逻。
	// 三条存活路径（正常处决完 / 中止但伤害已结算 / 起立安全超时）都经过这里；
	// 玩家已失效或目标不可达时 ForceEngageTarget 内部会安全降级。
	if (ABossAIController* BossController = Cast<ABossAIController>(Boss->GetController()))
	{
		BossController->ForceEngageTarget(ExecutingPlayer.Get());
	}

	ExecutingPlayer.Reset();

	UAbilityTask_PlayMontageAndWait* GetUpTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("BossExecutionGetUpMontage"),
			GetUpMontage,
			1.f,
			NAME_None,
			true,
			0.f);

	if (!GetUpTask)
	{
		bExecutionGetUpStarted = false;
		FinishExecutable(false);
		return;
	}

	GetUpTask->OnCompleted.AddDynamic(
		this,
		&ThisClass::HandleExecutionGetUpCompleted);
	GetUpTask->OnInterrupted.AddDynamic(
		this,
		&ThisClass::HandleExecutionGetUpInterrupted);
	GetUpTask->OnCancelled.AddDynamic(
		this,
		&ThisClass::HandleExecutionGetUpInterrupted);

	// 复用旧的安全计时器：进入起立前，处决阶段的旧计时器已经清除。
	Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	Boss->GetWorldTimerManager().SetTimer(
		ExecutionSafetyTimerHandle,
		this,
		&ThisClass::HandleExecutionGetUpSafetyTimeout,
		FMath::Max(GetUpMontage->GetPlayLength() + 0.5f, 0.5f),
		false);

	GetUpTask->ReadyForActivation();
}

void UGA_Boss_Executable::HandleExecutionGetUpCompleted()
{
	if (!IsActive() || !bExecutionGetUpStarted)
	{
		return;
	}

	if (ABossCharacter* Boss = GetBossCharacterFromActorInfo())
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	}

	bExecutionGetUpStarted = false;
	FinishExecutable(false);
}

void UGA_Boss_Executable::HandleExecutionGetUpInterrupted()
{
	if (!IsActive() || !bExecutionGetUpStarted)
	{
		return;
	}

	if (ABossCharacter* Boss = GetBossCharacterFromActorInfo())
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	}

	bExecutionGetUpStarted = false;
	FinishExecutable(true);
}

void UGA_Boss_Executable::HandleExecutionGetUpSafetyTimeout()
{
	if (!IsActive() || !bExecutionGetUpStarted)
	{
		return;
	}

	UE_LOG(LogTemp, Error,
		TEXT("Boss execution get-up montage timed out"));

	// 先清 Bool，EndAbility 停止 Montage 时即使触发 Interrupted 也不会重复结束。
	bExecutionGetUpStarted = false;
	FinishExecutable(true);
}

void UGA_Boss_Executable::HandleExecutionApplyDamage(FGameplayEventData Payload)
{
	if (!bExecutionStarted || bExecutionDamageApplied)
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	ADKCharacter* Player = ExecutingPlayer.Get();
	UFirstAbilitySystemComponent* BossASC =GetFirstAbilitySystemComponentFromActorInfo();
	UFirstAbilitySystemComponent* PlayerASC = Player? Player->GetFirstAbilitySystemComponent(): nullptr;
	UFirstAttributeSet* BossAttributes = Boss? Boss->GetFirstAttributeSet(): nullptr;

	if (!Boss || !Player || !BossASC || !PlayerASC || !BossAttributes)
	{
		return;
	}

	Boss->ClearPoiseRecoveryTimer();

	const float PlayerAttackPower = PlayerASC->GetNumericAttribute(UFirstAttributeSet::GetAttackPowerAttribute());
	const float ExecutionDamage =FMath::Max(PlayerAttackPower, 0.f) *FMath::Max(Player->GetExecutionDamageMultiplier(), 0.f);

	if (!FMath::IsFinite(ExecutionDamage) ||
		ExecutionDamage <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Execution damage is invalid. AttackPower=%.2f Multiplier=%.2f"),
			PlayerAttackPower,
			Player->GetExecutionDamageMultiplier());
		NotifyExecutingPlayerAbort();
		ReturnToExecutableAfterAbort();
		return;
	}

	FGameplayEffectContextHandle Context = PlayerASC->MakeEffectContext();
	Context.AddSourceObject(Player);
	Context.AddInstigator(Player, Player);

	FGameplayEffectSpecHandle Spec = PlayerASC->MakeOutgoingSpec(UFirstGE_ExecutionDamage::StaticClass(),1.f,Context);

	if (!Spec.IsValid())
	{
		UE_LOG(LogTemp, Error,TEXT("Failed to create execution damage GameplayEffect spec"));
		NotifyExecutingPlayerAbort();
		ReturnToExecutableAfterAbort();
		return;
	}

	Spec.Data->SetSetByCallerMagnitude(MyGameplayTags::Combat_SetByCaller_ExecutionDamage,ExecutionDamage);

	// AttributeSet 添加 Dead 与事件 Ability 激活是同步链路。
	// 因此必须在应用伤害前临时添加；若 BOSS 存活，结算后立即移除。
	const bool bAddedExecutedTag =!BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed);
	if (bAddedExecutedTag)
	{
		BossASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_Executed);
	}

	const float HealthBefore = BossAttributes->GetHealth();
	PlayerASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), BossASC);
	const float HealthAfter = BossAttributes->GetHealth();
	const bool bBossDead = BossASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead);

	// 非致死处决不能遗留 Executed；否则以后普通攻击致死也会误走处决死亡分支。
	if (!bBossDead && bAddedExecutedTag)
	{
		BossASC->RemoveLooseGameplayTag(MyGameplayTags::Boss_Status_Executed);
	}

	// Instant GE 的返回 Handle 不能证明数值真的生效；用 Health 前后值验证。
	if (!bBossDead && HealthAfter >= HealthBefore - KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Execution damage effect applied but Boss Health did not decrease"));
		NotifyExecutingPlayerAbort();
		ReturnToExecutableAfterAbort();
		return;
	}

	bExecutionDamageApplied = true;
	bBossKilledByExecution = bBossDead;
}

void UGA_Boss_Executable::HandleExecutableExpired(FGameplayEventData Payload)
{
	if (bExecutionStarted)
	{
		return;
	}

	FinishExecutable(false);
}

void UGA_Boss_Executable::RemoveBeingExecutedTag()
{
	if (!bOwnsBeingExecutedTag)
	{
		return;
	}

	if (UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted);
	}

	bOwnsBeingExecutedTag = false;
}

void UGA_Boss_Executable::NotifyExecutingPlayerAbort()
{
	if (bPlayerTerminalEventHandled)
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	ADKCharacter* Player = ExecutingPlayer.Get();
	if (!Boss || !Player)
	{
		return;
	}

	FGameplayEventData AbortEvent;
	AbortEvent.Instigator = Boss;
	AbortEvent.Target = Player;

	// 发送前先置位，防止同步 GameplayEvent 回调造成重复通知。
	bPlayerTerminalEventHandled = true;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Player,MyGameplayTags::DK_Event_ExecutionAbortedByBoss,AbortEvent);
}

void UGA_Boss_Executable::ReturnToExecutableAfterAbort()
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (Boss)
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	}
	RemoveBeingExecutedTag();

	bExecutionStarted = false;
	bExecutionDamageApplied = false;
	bBossKilledByExecution = false;
	bPlayerTerminalEventHandled = false;
	bExecutionGetUpStarted = false;
	ExecutingPlayer.Reset();

	StopBossMovement();
	PlayExecutableLoop();

	if (Boss)
	{
		// 重新给玩家完整可处决窗口（ExecutableWindowDuration），
		// 而不是沿用中止前已经消耗的时间。
		Boss->RestartExecutableWindowTimer();
	}
}

void UGA_Boss_Executable::HandleExecutionSafetyTimeout()
{
	if (!bExecutionStarted || bExecutionGetUpStarted)
	{
		return;
	}

	if (bExecutionDamageApplied)
	{
		NotifyExecutingPlayerAbort();

		UFirstAbilitySystemComponent* ASC =
			GetFirstAbilitySystemComponentFromActorInfo();
		const bool bBossDead = !ASC || ASC->HasMatchingGameplayTag(
			MyGameplayTags::Shared_Status_Dead);

		if (!bBossKilledByExecution && !bBossDead)
		{
			StartExecutionGetUp();
			return;
		}

		FinishExecutable(false);
		return;
	}

	UE_LOG(LogTemp, Error,TEXT("Boss execution timed out before ApplyDamage/terminal event"));
	NotifyExecutingPlayerAbort();
	ReturnToExecutableAfterAbort();
}

void UGA_Boss_Executable::FinishExecutable(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,bWasCancelled);
}

void UGA_Boss_Executable::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();

	// 必须先停安全计时器、关闭起立回调门，再停止 Montage。
	if (Boss)
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	}
	bExecutionGetUpStarted = false;

	const bool bMustReleaseExecutingPlayer =
		bExecutionStarted && !bPlayerTerminalEventHandled;

	if (bMustReleaseExecutingPlayer)
	{
		NotifyExecutingPlayerAbort();
	}

	RemoveBeingExecutedTag();

	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	const bool bBossDead = ASC && ASC->HasMatchingGameplayTag(
		MyGameplayTags::Shared_Status_Dead);
	UAnimInstance* AnimInstance = Boss && Boss->GetMesh()
		? Boss->GetMesh()->GetAnimInstance()
		: nullptr;

	// 只有本次处决伤害真正致死时，才保留 Target Montage 最后一帧。
	if (AnimInstance && Boss && !bBossKilledByExecution)
	{
		if (UAnimMontage* WeakMontage = Boss->GetExecutableMontage())
		{
			AnimInstance->Montage_Stop(0.15f, WeakMontage);
		}
		if (UAnimMontage* TargetMontage = Boss->GetExecutionTargetMontage())
		{
			AnimInstance->Montage_Stop(0.15f, TargetMontage);
		}
		if (UAnimMontage* GetUpMontage = Boss->GetExecutionGetUpMontage())
		{
			AnimInstance->Montage_Stop(0.1f, GetUpMontage);
		}
	}

	if (Boss)
	{
		if (UBossCombatComponent* Combat = Boss->GetBossCombatComponent())
		{
			Combat->ToggleWeaponCollision(false);
		}

		if (!bBossDead)
		{
			Boss->RestorePoiseToFull();
		}
	}

	ExecutingPlayer.Reset();

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}