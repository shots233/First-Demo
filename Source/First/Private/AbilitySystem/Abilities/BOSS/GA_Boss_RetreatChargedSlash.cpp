// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Abilities/BOSS/GA_Boss_RetreatChargedSlash.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/GameplayEffects/FirstGE_BossCooldown.h"
#include "AbilitySystem/GameplayEffects/FirstGE_Damage.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "Components/Combat/BossCombatComponent.h"
#include "Controller/BossAIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/Weapons/FirstWeaponBase.h"
#include "MyGameplayTags.h"
#include "TimerManager.h"

UGA_Boss_RetreatChargedSlash::UGA_Boss_RetreatChargedSlash()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack);
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack_RetreatChargedSlash);
	SetAssetTags(AssetTags);

	ActivationRequiredTags.AddTag(MyGameplayTags::Boss_Status_WeaponDrawn);

	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_DrawSword);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_BeingExecuted);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executed);

	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);

	CooldownGameplayEffectClass = UFirstGE_BossCooldown::StaticClass();
	CooldownTags.AddTag(
		MyGameplayTags::Boss_Cooldown_Attack_RetreatChargedSlash);
	CooldownDuration = 10.f;

	// 接招链默认接五连斩；GA 类默认值里换标签即可改接其它招式（低耦合）。
	ChainAttackTag = MyGameplayTags::Boss_Ability_Attack_FiveCombo;

	// 目标超过 900cm 时仍保留 AttackTarget，但实际前冲始终封顶为 650cm。
	// 实际终点仍由双方胶囊半径和 SurfaceGap 计算，不会把 BOSS 拉进目标身体。
	AttackWarpingData.MaxWarpDistance = 900.f;
	AttackWarpingData.SurfaceGap = 30.f;
	AttackWarpingData.MaxWarpTravelDistance = 650.f;
	// 这招有完整蓄力预警；玩家在 Commit 前跑出范围时，Slash 仍冲到
	// 最大前冲边界，而不是删除 AttackTarget 后退化成原地挥刀。
	AttackWarpingData.bKeepWarpTargetWhenBeyondMaxDistance = true;
	AttackWarpingData.FacingTurnRate = 720.f;
	AttackWarpingData.MaxFacingAngle = 30.f;
	AttackWarpingData.UpdateInterval = 0.02f;
	AttackWarpingData.bTrackTarget = false;

	bIsCancelable = true;
}

bool UGA_Boss_RetreatChargedSlash::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(
		Handle,
		ActorInfo,
		SourceTags,
		TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	const ABossCharacter* Boss = ActorInfo
		? Cast<ABossCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;
	if (!Boss || !Boss->GetRetreatChargedSlashMontage())
	{
		return false;
	}

	ACharacter* Target = ResolveCurrentTarget(Boss);
	if (!IsTargetUsable(Target))
	{
		return false;
	}

	const ABossAIController* BossAI =
		Cast<ABossAIController>(Boss->GetController());
	if (BossAI && BossAI->IsTargetNavigationBlocked(Target))
	{
		return false;
	}

	return Boss->HasSafeRetreatSpace(
		RetreatCheckDistance,
		RetreatNavTolerance,
		RetreatMaxLandingHeightDelta);
}

void UGA_Boss_RetreatChargedSlash::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bCommitReceived = false;
	bHitResolved = false;
	bDirectionLockApplied = false;
	CommittedTarget.Reset();

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UAnimMontage* Montage = Boss
		? Boss->GetRetreatChargedSlashMontage()
		: nullptr;
	ACharacter* Target = ResolveCurrentTarget(Boss);

	ABossAIController* BossAI = Boss
		? Cast<ABossAIController>(Boss->GetController())
		: nullptr;
	const bool bNavigationBlocked = BossAI &&
		BossAI->RefreshTargetNavigationBlocked(Target);

	if (!Boss || !Montage || !IsTargetUsable(Target) ||
		bNavigationBlocked ||
		!Boss->HasSafeRetreatSpace(
			RetreatCheckDistance,
			RetreatNavTolerance,
			RetreatMaxLandingHeightDelta))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	CommittedTarget = Target;

	const float ActivationDistance = FVector::Dist2D(
		Boss->GetActorLocation(),
		Target->GetActorLocation());
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[BossAbilityTrace] RetreatChargedSlash activated | Boss=%s | Target=%s | Distance=%.1f | RetreatCheckDistance=%.1f"),
		*GetNameSafe(Boss),
		*GetNameSafe(Target),
		ActivationDistance,
		RetreatCheckDistance);

	SetAttackDirectionLocked(true);
	Boss->SetBossRotationMode(EBossRotationMode::Frozen);

	if (AAIController* AIC = Cast<AAIController>(Boss->GetController()))
	{
		AIC->StopMovement();
		AIC->ClearFocus(EAIFocusPriority::Gameplay);
	}
	if (UCharacterMovementComponent* Movement = Boss->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	UAbilityTask_WaitGameplayEvent* HitTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_MeleeHit,
			nullptr,
			false,
			true);
	HitTask->EventReceived.AddDynamic(this, &ThisClass::HandleMeleeHit);
	HitTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* ChargeBeginTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_RetreatChargedSlash_ChargeBegin,
			nullptr,
			true,
			true);
	ChargeBeginTask->EventReceived.AddDynamic(
		this,
		&ThisClass::HandleChargeBegin);
	ChargeBeginTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* CommitTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_RetreatChargedSlash_Commit,
			nullptr,
			true,
			true);
	CommitTask->EventReceived.AddDynamic(
		this,
		&ThisClass::HandleAttackCommit);
	CommitTask->ReadyForActivation();

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("RetreatChargedSlashMontage"),
			Montage);
	MontageTask->OnCompleted.AddDynamic(
		this,
		&ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(
		this,
		&ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(
		this,
		&ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UGA_Boss_RetreatChargedSlash::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (UBossCombatComponent* Combat = GetBossCombatComponentFromActorInfo())
	{
		Combat->ToggleWeaponCollision(false);
	}

	DisableWeaponAttackEffects();

	if (ABossCharacter* Boss = GetBossCharacterFromActorInfo())
	{
		Boss->EndAttackWarping();
	}

	SetAttackDirectionLocked(false);

	if (UAbilitySystemComponent* ASC = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr)
	{
		ASC->SetLooseGameplayTagCount(
			MyGameplayTags::Boss_Status_HitReactWindow,
			0);
	}

	CommittedTarget.Reset();
	bCommitReceived = false;
	bHitResolved = false;
	bDirectionLockApplied = false;

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}

const FGameplayTagContainer*
UGA_Boss_RetreatChargedSlash::GetCooldownTags() const
{
	return &CooldownTags;
}

void UGA_Boss_RetreatChargedSlash::HandleChargeBegin(
	FGameplayEventData Payload)
{
	if (!IsActive() || bCommitReceived)
	{
		return;
	}

	SetAttackDirectionLocked(false);

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!Boss)
	{
		return;
	}

	Boss->SetBossRotationMode(EBossRotationMode::FaceTarget);

	AAIController* AIC = Cast<AAIController>(Boss->GetController());
	ACharacter* Target = CommittedTarget.Get();
	ABossAIController* BossAI = Cast<ABossAIController>(AIC);
	const bool bNavigationBlocked = BossAI &&
		BossAI->RefreshTargetNavigationBlocked(Target);

	if (IsTargetUsable(Target) && !bNavigationBlocked)
	{
		// 后撤已经结束，此时提前建立并跟踪 AttackTarget。
		// Commit 会再次刷新并切换为固定目标，保证 Warp 窗口首帧不会因目标尚未创建而永久 Disabled。
		FFirstAttackWarpingData TrackingWarpingData = AttackWarpingData;
		TrackingWarpingData.bTrackTarget = true;
		Boss->BeginAttackWarping(Target, TrackingWarpingData);

		const float ChargeWarpDistance = FVector::Dist2D(
			Boss->GetActorLocation(),
			Target->GetActorLocation());
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[BossAbilityTrace] RetreatChargedSlash ChargeBegin prepared warp target | Boss=%s | Target=%s | Distance=%.1f | MaxWarpDistance=%.1f | PositionWarp=%s"),
			*GetNameSafe(Boss),
			*GetNameSafe(Target),
			ChargeWarpDistance,
			AttackWarpingData.MaxWarpDistance,
			TrackingWarpingData.bKeepWarpTargetWhenBeyondMaxDistance
				? TEXT("ClampToMaxTravel")
				: (ChargeWarpDistance <= TrackingWarpingData.MaxWarpDistance
					? TEXT("Enabled")
					: TEXT("DisabledByDistance")));

		if (AIC)
		{
			AIC->SetFocus(Target, EAIFocusPriority::Gameplay);
		}
	}
	else
	{
		Boss->EndAttackWarping();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[BossAbilityTrace] RetreatChargedSlash ChargeBegin could not prepare warp target | Target=%s | NavigationBlocked=%s"),
			*GetNameSafe(Target),
			bNavigationBlocked ? TEXT("true") : TEXT("false"));

		if (AIC)
		{
			AIC->ClearFocus(EAIFocusPriority::Gameplay);
		}
	}
}

void UGA_Boss_RetreatChargedSlash::HandleAttackCommit(
	FGameplayEventData Payload)
{
	if (!IsActive() || bCommitReceived)
	{
		return;
	}
	bCommitReceived = true;

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!Boss)
	{
		return;
	}

	SetAttackDirectionLocked(true);
	Boss->SetBossRotationMode(EBossRotationMode::Frozen);

	ABossAIController* BossAI =
		Cast<ABossAIController>(Boss->GetController());
	if (BossAI)
	{
		BossAI->StopMovement();
		BossAI->ClearFocus(EAIFocusPriority::Gameplay);
	}

	ACharacter* Target = CommittedTarget.Get();
	if (!IsTargetUsable(Target))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[BossAbilityTrace] RetreatChargedSlash Commit rejected | Invalid committed target"));
		Boss->EndAttackWarping();
		EndAbility(
			CurrentSpecHandle,
			CurrentActorInfo,
			CurrentActivationInfo,
			true,
			true);
		return;
	}

	// 在承诺帧同步复查目标导航状态；不可达时统一取消，避免旋转服务与方向锁争权。
	if (BossAI && BossAI->RefreshTargetNavigationBlocked(Target))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[BossAbilityTrace] RetreatChargedSlash Commit rejected | Target navigation blocked | Target=%s"),
			*GetNameSafe(Target));
		EndAbility(
			CurrentSpecHandle,
			CurrentActorInfo,
			CurrentActivationInfo,
			true,
			true);
		return;
	}

	FFirstAttackWarpingData FixedWarpingData = AttackWarpingData;
	FixedWarpingData.bTrackTarget = false;
	Boss->BeginAttackWarping(Target, FixedWarpingData);

	const float CommitWarpDistance = FVector::Dist2D(
		Boss->GetActorLocation(),
		Target->GetActorLocation());
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[BossAbilityTrace] RetreatChargedSlash Commit received | Boss=%s | Target=%s | Distance=%.1f | MaxWarpDistance=%.1f | PositionWarp=%s"),
		*GetNameSafe(Boss),
		*GetNameSafe(Target),
		CommitWarpDistance,
		AttackWarpingData.MaxWarpDistance,
		FixedWarpingData.bKeepWarpTargetWhenBeyondMaxDistance
			? TEXT("ClampToMaxTravel")
			: (CommitWarpDistance <= FixedWarpingData.MaxWarpDistance
				? TEXT("Enabled")
				: TEXT("DisabledByDistance")));
}

void UGA_Boss_RetreatChargedSlash::HandleMeleeHit(
	FGameplayEventData Payload)
{
	if (!IsActive() || !bCommitReceived || bHitResolved)
	{
		return;
	}

	AActor* TargetActor = const_cast<AActor*>(Payload.Target.Get());
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!TargetActor || !Boss)
	{
		return;
	}
	bHitResolved = true;

	const EFirstDefenseResult DefenseResult = ResolveTargetDefense(
		TargetActor,
		Boss->GetRetreatChargedSlashDefenseData());
	if (DefenseResult != EFirstDefenseResult::Damaged)
	{
		return;
	}

	// 判定为真实命中（玩家未防御成功）：播放 BP_Boss 上配置的命中音效与血花。
	PlayBossHitPlayerFeedback(TargetActor);

	FGameplayEffectSpecHandle SpecHandle = MakeBossDamageEffectSpecHandle(
		UFirstGE_Damage::StaticClass(),
		Boss->GetRetreatChargedSlashDamage());
	ApplyEffectSpecHandleToTarget(TargetActor, SpecHandle);
}

void UGA_Boss_RetreatChargedSlash::HandleMontageCompleted()
{
	if (!IsActive())
	{
		return;
	}

	// 接招只发生在"正常演出完毕"路径：破韧/死亡等打断走 HandleMontageInterrupted，
	// 绝不接招。掷点与调度必须在 EndAbility 之前——本能力的 CurrentActorInfo
	// 在结束后不再可靠。
	TryScheduleChainAttack();

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		false);
}

void UGA_Boss_RetreatChargedSlash::HandleMontageInterrupted()
{
	if (IsActive())
	{
		EndAbility(
			CurrentSpecHandle,
			CurrentActorInfo,
			CurrentActivationInfo,
			true,
			true);
	}
}

void UGA_Boss_RetreatChargedSlash::TryScheduleChainAttack()
{
	// 概率设 0 或标签未配置即视为关闭接招。
	if (!ChainAttackTag.IsValid() || ChainAttackChance <= 0.f)
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UAbilitySystemComponent* ASC = CurrentActorInfo
		? CurrentActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	if (!Boss || !ASC)
	{
		return;
	}

	// 挥空也接招（跟踪性由接续招式的 Motion Warping 吸附负责）；
	// 但目标缺失/已死亡时接招没有意义。复用本 GA 现成的目标工具函数。
	ACharacter* Target = ResolveCurrentTarget(Boss);
	if (!IsTargetUsable(Target))
	{
		return;
	}

	if (FMath::FRand() >= ChainAttackChance)
	{
		return;
	}

	// 延迟期间保持 Boss.Status.Attacking：这是 BT 的 bIsBusy 判定来源，
	// 压住接招间隙，避免行为树抢在接续招式前选新招/恢复移动/转身。
	// 本能力的 ActivationOwnedTags 会在随后的 EndAbility 里摘掉该标签，
	// 这里补一个 Loose 计数把窗口续上；接续失败路径统一归还。
	ASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_Attacking);

	TWeakObjectPtr<UAbilitySystemComponent> WeakASC(ASC);
	const FGameplayTag TagToChain = ChainAttackTag;
	FTimerHandle ChainTimerHandle;
	Boss->GetWorldTimerManager().SetTimer(
		ChainTimerHandle,
		FTimerDelegate::CreateWeakLambda(Boss,
			[WeakASC, TagToChain]()
			{
				UAbilitySystemComponent* ASCPtr = WeakASC.Get();
				if (!ASCPtr)
				{
					return;
				}

				// 无论激活成败先归还延迟期间的忙碌标签：
				// 成功时接续招式自己的 OwnedTags 已把 Attacking 续上；
				// 失败时行为树需要立刻恢复调度。
				ASCPtr->RemoveLooseGameplayTag(
					MyGameplayTags::Boss_Status_Attacking);

				FGameplayTagContainer ChainTags;
				ChainTags.AddTag(TagToChain);

				// 激活失败（目标死亡/冷却中/被阻挡）静默放弃：
				// 独立释放路径与接招路径互不干扰（五连斩自身冷却兜底）。
				ASCPtr->TryActivateAbilitiesByTag(ChainTags, false);
			}),
		FMath::Max(ChainAttackDelay, 0.f),
		false);
}

ACharacter* UGA_Boss_RetreatChargedSlash::ResolveCurrentTarget(
	const ABossCharacter* Boss) const
{
	AAIController* AIC = Boss
		? Cast<AAIController>(Boss->GetController())
		: nullptr;
	UBlackboardComponent* Blackboard = AIC
		? AIC->GetBlackboardComponent()
		: nullptr;

	return Blackboard
		? Cast<ACharacter>(Blackboard->GetValueAsObject(TEXT("TargetActor")))
		: nullptr;
}

bool UGA_Boss_RetreatChargedSlash::IsTargetUsable(
	const ACharacter* Target) const
{
	if (!IsValid(Target) || Target->IsActorBeingDestroyed())
	{
		return false;
	}

	const UAbilitySystemComponent* TargetASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(
			const_cast<ACharacter*>(Target));

	return !TargetASC || !TargetASC->HasMatchingGameplayTag(
		MyGameplayTags::Shared_Status_Dead);
}

void UGA_Boss_RetreatChargedSlash::SetAttackDirectionLocked(
	bool bShouldLock)
{
	if (bDirectionLockApplied == bShouldLock)
	{
		return;
	}

	UAbilitySystemComponent* ASC = CurrentActorInfo
		? CurrentActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	if (!ASC)
	{
		return;
	}

	if (bShouldLock)
	{
		ASC->AddLooseGameplayTag(
			MyGameplayTags::Boss_Status_AttackDirectionLocked);
	}
	else
	{
		ASC->RemoveLooseGameplayTag(
			MyGameplayTags::Boss_Status_AttackDirectionLocked);
	}

	bDirectionLockApplied = bShouldLock;
}

void UGA_Boss_RetreatChargedSlash::DisableWeaponAttackEffects()
{
	UBossCombatComponent* Combat = GetBossCombatComponentFromActorInfo();
	if (!Combat)
	{
		return;
	}

	AFirstWeaponBase* Weapon = Combat->GetCharacterCurrentEquippedWeapon();
	if (!Weapon)
	{
		Weapon = Combat->GetCharacterCarriedWeaponByTag(
			MyGameplayTags::Boss_Weapon_Sword);
	}

	if (Weapon)
	{
		Weapon->SetWeaponTelegraphEnabled(false);
		Weapon->SetSlashTrailEnabled(false);
	}
}
