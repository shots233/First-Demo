// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/BOSS/GA_Boss_FourCombo.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/GameplayEffects/FirstGE_Damage.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "GameFramework/Character.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/GameplayEffects/FirstGE_BossCooldown.h"
#include "Components/Combat/BossCombatComponent.h"

UGA_Boss_FourCombo::UGA_Boss_FourCombo()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 身份 + 类别标签：父标签 Boss.Ability.Attack 保证破韧（Executable）与
	// 非霸体弹反（ParryStagger）的统一取消路径能覆盖本招式。
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack);
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack_FourCombo);
	SetAssetTags(AssetTags);

	ActivationRequiredTags.AddTag(MyGameplayTags::Boss_Status_WeaponDrawn);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);
	// 攻击期间挂"攻击中"标签：行为树据此挡住追击/巡逻，避免边攻击边位移。
	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);

	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_BeingExecuted);

	CooldownGameplayEffectClass = UFirstGE_BossCooldown::StaticClass();
	CooldownTags.AddTag(MyGameplayTags::Boss_Cooldown_Attack_FourCombo);
	CooldownDuration = 8.f;

	// 可被系统级取消（破韧/死亡）；玩家普攻与弹反的中断路径
	// 已由 HitReact 阻挡标签与 ParryStagger 霸体分支拦下。
	bIsCancelable = true;
}

void UGA_Boss_FourCombo::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UAnimMontage* Montage = Boss ? Boss->GetFourComboMontage() : nullptr;
	if (!Boss || !Montage)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// CommitAbility 才会真正应用该招式配置的 Cost / Cooldown。
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 四连共享一个动态吸附会话；每个 Motion Warping 窗口都会读取持续更新后的目标。
	ACharacter* WarpTarget = nullptr;
	if (AAIController* AIC = Cast<AAIController>(Boss->GetController()))
	{
		if (UBlackboardComponent* BB = AIC->GetBlackboardComponent())
		{
			WarpTarget = Cast<ACharacter>(BB->GetValueAsObject(TEXT("TargetActor")));
		}
	}
	Boss->BeginAttackWarping(WarpTarget, AttackWarpingData);

	// 整个四连期间都监听命中。
	UAbilityTask_WaitGameplayEvent* HitTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_MeleeHit,
			nullptr,
			false,
			true);
	HitTask->EventReceived.AddDynamic(this, &ThisClass::HandleMeleeHit);
	HitTask->ReadyForActivation();

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("FourComboMontage"),
			Montage);
	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UGA_Boss_FourCombo::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// 四连所有 Warp 窗口共用同一会话，能力的任意结束路径在这里统一清理。
	if (ABossCharacter* Boss = GetBossCharacterFromActorInfo())
	{
		Boss->EndAttackWarping();
	}

	if (UBossCombatComponent* Combat = GetBossCombatComponentFromActorInfo())
	{
		Combat->ToggleWeaponCollision(false);
	}

	// 霸体兜底：蒙太奇被打断且引擎漏发 ANS_SuperArmorWindow::NotifyEnd 时，
	// 确保 Boss.Status.Uninterruptible 不残留。SetCount(0) 幂等；
	// 全项目霸体来源只有四连斩一处，不会误伤其它计数。
	// 写法对齐 GA_Boss_RetreatChargedSlash 对 HitReactWindow 的同款兜底。
	if (UAbilitySystemComponent* ASC =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
	{
		ASC->SetLooseGameplayTagCount(MyGameplayTags::Boss_Status_Uninterruptible, 0);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Boss_FourCombo::HandleMeleeHit(FGameplayEventData Payload)
{
	AActor* TargetActor = const_cast<AActor*>(Payload.Target.Get());
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!TargetActor || !Boss)
	{
		return;
	}

	const EFirstDefenseResult DefenseResult = ResolveTargetDefense(TargetActor, Boss->GetFourComboDefenseData());

	if (DefenseResult != EFirstDefenseResult::Damaged)
	{
		return;
	}

	// 判定为真实命中（玩家未防御成功）：播放 BP_Boss 上配置的命中音效与血花。
	PlayBossHitPlayerFeedback(TargetActor);

	FGameplayEffectSpecHandle SpecHandle = MakeBossDamageEffectSpecHandle(UFirstGE_Damage::StaticClass(), Boss->GetFourComboDamagePerHit());

	ApplyEffectSpecHandleToTarget(TargetActor, SpecHandle);
}

void UGA_Boss_FourCombo::HandleMontageCompleted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_Boss_FourCombo::HandleMontageInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

const FGameplayTagContainer* UGA_Boss_FourCombo::GetCooldownTags() const
{
	return &CooldownTags;
}
