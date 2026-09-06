// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/Boss/GA_Boss_HitReact.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/BossCharacter.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/GameplayEffects/FirstGE_BossCooldown.h"

UGA_Boss_HitReact::UGA_Boss_HitReact()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_HitReact);
	SetAssetTags(AssetTags);

	// 受击期间短暂硬直；不能叠加触发；死亡后不再受击。
	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	// 霸体窗口（ANS_SuperArmorWindow）期间即使动画误放了受击窗口也不触发打断。
	// 双保险：四连斩前四段本就不开放受击窗口，第四段后摇的惩罚窗口不受影响
	//（霸体窗口与受击窗口在蒙太奇里错开摆放）。
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Uninterruptible);
	
	// 只有攻击后摇的受击窗口内才可被打断（D29）。
	// ActivationRequiredTags：激活前置条件，身上没有这个标签，能力就不会激活。
	ActivationRequiredTags.AddTag(MyGameplayTags::Boss_Status_HitReactWindow);

	// 受击冷却：防止窗口内连续命中无限僵直。
	// 冷却 GE、冷却标签、冷却时长三者配合，GAS 会自动阻止冷却期间再次激活。
	CooldownGameplayEffectClass = UFirstGE_BossCooldown::StaticClass();
	CooldownTags.AddTag(MyGameplayTags::Boss_Cooldown_HitReact);
	CooldownDuration = 0.8f;

	// 事件触发：ABossCharacter 掉血时发送 Boss.Event.HitReact。
	FAbilityTriggerData Trigger;   // ← 原来是 FGameplayAbilityTrigger
	Trigger.TriggerTag = MyGameplayTags::Boss_Event_HitReact;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UGA_Boss_HitReact::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                        const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                        const FGameplayEventData* TriggerEventData)
{
	
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABossCharacter* Boss = Cast<ABossCharacter>(GetAvatarActorFromActorInfo());
	if (!Boss)
	{
		FinishHitReact(true);
		return;
	}

	// CommitAbility 才会真正应用上面配置的受击冷却 GE（0.8s）。
	// 缺失时冷却永远不存在，受击窗口内连续命中会无限打断 BOSS 后摇。
	// 口径与攻击类 GA（NormalAttack/ThreeCombo 等）一致。
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishHitReact(true);
		return;
	}

	// 打断 BOSS 当前招式（以后 BOSS 攻击能力都要挂 Boss.Ability.Attack 标签）。
	if (UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackTags;
		AttackTags.AddTag(MyGameplayTags::Boss_Ability_Attack);
		ASC->CancelAbilities(&AttackTags, nullptr, this);
	}

	UAnimMontage* Montage = Boss->GetHitReactMontage();
	if (!Montage)
	{
		FinishHitReact(true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("HitReactMontage"),
			Montage);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UGA_Boss_HitReact::HandleMontageCompleted()
{
	FinishHitReact(false);
}

void UGA_Boss_HitReact::HandleMontageInterrupted()
{
	FinishHitReact(true);
}

void UGA_Boss_HitReact::FinishHitReact(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		bWasCancelled);
}

const FGameplayTagContainer* UGA_Boss_HitReact::GetCooldownTags() const
{
	return &CooldownTags;
}
