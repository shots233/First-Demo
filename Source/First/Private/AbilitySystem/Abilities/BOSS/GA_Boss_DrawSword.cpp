// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Abilities/Boss/GA_Boss_DrawSword.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/BossCharacter.h"
#include "Components/Combat/BossCombatComponent.h"
#include "Items/Weapons/FirstWeaponBase.h"
#include "MyGameplayTags.h"

UGA_Boss_DrawSword::UGA_Boss_DrawSword()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 身份标签：AbilityTags（供按标签激活）、AssetTags（供取消/调试）。
	AbilityTags.AddTag(MyGameplayTags::Boss_Ability_DrawSword);
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_DrawSword);
	SetAssetTags(AssetTags);

	// 拔过就不能再拔；死亡不拔。
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_WeaponDrawn);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
	// 拔剑期间挂"拔剑中"标签：供 Update Boss State 聚合为 bIsBusy，挡住追击/巡逻。
	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_DrawSword);

	// CombatStart 事件触发。
	FAbilityTriggerData Trigger;   // ← 原来是 FGameplayAbilityTrigger
	Trigger.TriggerTag = MyGameplayTags::Boss_Event_CombatStart;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UGA_Boss_DrawSword::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                         const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                         const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!Boss)
	{
		FinishDraw(true);
		return;
	}

	bWeaponAttached = false;

	// 监听拔剑蒙太奇里的挂手 Notify（复用 DK.Event.Weapon.AttachToHand）。
	UAbilityTask_WaitGameplayEvent* AttachTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_Weapon_AttachToHand,
			nullptr,
			false,
			true);
	AttachTask->EventReceived.AddDynamic(this, &ThisClass::HandleWeaponAttachEvent);
	AttachTask->ReadyForActivation();

	UAnimMontage* Montage = Boss->GetDrawSwordMontage();
	if (!Montage)
	{
		// 没有拔剑动画时直接挂手，保证流程可跑通。
		AttachWeaponToHand();
		FinishDraw(false);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("DrawSwordMontage"),
			Montage);
	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UGA_Boss_DrawSword::HandleWeaponAttachEvent(FGameplayEventData Payload)
{
	AttachWeaponToHand();
}

void UGA_Boss_DrawSword::HandleMontageCompleted()
{
	AttachWeaponToHand();
	FinishDraw(false);
}

void UGA_Boss_DrawSword::HandleMontageInterrupted()
{
	AttachWeaponToHand();
	FinishDraw(true);
}

void UGA_Boss_DrawSword::AttachWeaponToHand()
{
		if (bWeaponAttached)
		{
			return;
		}
		bWeaponAttached = true;

		ABossCharacter* Boss = GetBossCharacterFromActorInfo();
		UBossCombatComponent* Combat = GetBossCombatComponentFromActorInfo();
		if (!Boss || !Combat || !Boss->GetMesh())
		{
			return;
		}

		if (AFirstWeaponBase* Weapon = Combat->GetCharacterCarriedWeaponByTag(MyGameplayTags::Boss_Weapon_Sword))
		{
			Weapon->AttachToComponent(
				Boss->GetMesh(),
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				Boss->HandSocketName);
		}

		// 标记当前装备武器：武器碰撞开关（ToggleWeaponCollision）依赖这个标签。
		Combat->CurrentEquippedWeaponTag = MyGameplayTags::Boss_Weapon_Sword;

		if (UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo())
		{
			ASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_WeaponDrawn);
		}
}

void UGA_Boss_DrawSword::FinishDraw(bool bWasCancelled)
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
