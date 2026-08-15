// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/DK//FirstGA_DKEquipSword.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/DKCharacter.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Items/Weapons/DKWeapon.h"
#include "MyGameplayTags.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"

UFirstGA_DKEquipSword::UFirstGA_DKEquipSword()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Equip_Sword);
	SetAssetTags(AssetTags);

	// 装备动画播放期间持有切换状态，攻击/闪避会把它列为 BlockedTag。
	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Attacking);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
}

void UFirstGA_DKEquipSword::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo, 
	const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	// UFirstGA_DKEquipSword 使用 InstancedPerActor。
	// 同一个 Ability 对象会在以后再次被激活，因此每次开始前必须重置本次状态。
	bSwordAttachedToHand = false;
	
	ADKCharacter* DKCharacter = GetDKCharacterFromActorInfo();
	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	ADKWeapon* Weapon = CombatComponent? CombatComponent->GetDKCarriedWeaponByTag(MyGameplayTags::DK_Weapon_Sword): nullptr;
	
	if (!DKCharacter || !CombatComponent || !ASC || !Weapon)
	{
		FinishEquip(true);
		return;
	}
	
	// 重复按 E 时不重复授予攻击 Ability；否则同一输入会匹配多份 Spec。
	if (CombatComponent->CurrentEquippedWeaponTag == MyGameplayTags::DK_Weapon_Sword)
	{
		FinishEquip(false);
		return;
	}
	
	const FName HandSocket = Weapon->DKWeaponData.EquippedSocketName;

	// Montage 只负责表现。没有配置时，装备功能仍然可以完整工作并立即结束。
	if (UAnimMontage* EquipMontage = Weapon->DKWeaponData.EquipMontage)
	{
		// 必须先监听，再开始播放 Montage；否则很短的动画可能先触发 Notify，
		// 而监听器还没有准备好，导致事件丢失。
		UAbilityTask_WaitGameplayEvent* AttachTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, MyGameplayTags::DK_Event_Weapon_AttachToHand, nullptr, true, true);
		AttachTask->EventReceived.AddDynamic(this, &ThisClass::HandleAttachToHand);
		AttachTask->ReadyForActivation();
		
		UAbilityTask_PlayMontageAndWait* MontageTask =UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,TEXT("EquipSwordMontage"),EquipMontage);

		MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageCancelled);
		MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageCancelled);
		MontageTask->ReadyForActivation();
		return;
	}

	// 未配置 Montage 时不会收到 Notify；保留即时装备，便于无动画调试。
	if (!AttachSwordToHand())
	{
		FinishEquip(true);
		return;
	}

	FinishEquip(false);
	
}

void UFirstGA_DKEquipSword::HandleAttachToHand(FGameplayEventData Payload)
{
	if (!AttachSwordToHand())
	{
		FinishEquip(true);
	}
}

bool UFirstGA_DKEquipSword::AttachSwordToHand()
{
	// 双保险：Notify 意外重复时，不重复 GiveAbility。
	if (bSwordAttachedToHand)
	{
		return true;
	}
	
	ADKCharacter* DKCharacter = GetDKCharacterFromActorInfo();
	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	
	ADKWeapon* Weapon = CombatComponent? CombatComponent->GetDKCarriedWeaponByTag(MyGameplayTags::DK_Weapon_Sword): nullptr;
	if (!DKCharacter || !CombatComponent || !ASC || !Weapon)
	{
		return false;
	}
	
	const FName HandSocket = Weapon->DKWeaponData.EquippedSocketName;
	
	if (!DKCharacter->GetMesh()->DoesSocketExist(HandSocket) || !Weapon->AttachToComponent(
		DKCharacter->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale,HandSocket))
	{
		UE_LOG(LogTemp, Error,TEXT("Sword hand socket is invalid: %s"),*HandSocket.ToString());
		return false;
	}
	
	// 画面 Attach 成功后，才同步玩法状态。
	CombatComponent->CurrentEquippedWeaponTag = MyGameplayTags::DK_Weapon_Sword;
	
	TArray<FGameplayAbilitySpecHandle> GrantedHandles;
	ASC->GrantWeaponAbilities(Weapon->DKWeaponData.DefaultWeaponAbilities, GetAbilityLevel(), GrantedHandles);
	Weapon->AssignGrantedAbilitySpecHandles(GrantedHandles);
	
	bSwordAttachedToHand = true;
	return true;
}

void UFirstGA_DKEquipSword::HandleMontageCompleted()
{
	// 正常播完却未 Attach，说明 Montage 中漏放了 Notify。
	FinishEquip(!bSwordAttachedToHand);
}

void UFirstGA_DKEquipSword::HandleMontageCancelled()
{
	// Notify 前取消：剑保持背部状态；Notify 后取消：剑已在手部。
	FinishEquip(true);
}

void UFirstGA_DKEquipSword::FinishEquip(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,bWasCancelled);
}
