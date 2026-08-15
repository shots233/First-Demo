// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/DK/FirstGA_DKUnequipSword.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/DKCharacter.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Items/Weapons/DKWeapon.h"
#include "MyGameplayTags.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"

UFirstGA_DKUnequipSword::UFirstGA_DKUnequipSword()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Unequip_Sword);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Attacking);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
}

void UFirstGA_DKUnequipSword::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bSwordAttachedToBack = false;
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	ADKCharacter* DKCharacter = GetDKCharacterFromActorInfo();
	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	ADKWeapon* Weapon = CombatComponent? CombatComponent->GetDKCurrentEquippedWeapon(): nullptr;

	if (!DKCharacter || !CombatComponent || !ASC || !Weapon)
	{
		FinishUnequip(false);
		return;
	}
	
	if (UAnimMontage* UnequipMontage = Weapon->DKWeaponData.UnequipMontage)
	{
		UAbilityTask_WaitGameplayEvent* AttachTask =UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,MyGameplayTags::DK_Event_Weapon_AttachToBack,nullptr,true,true);
		AttachTask->EventReceived.AddDynamic(this,&ThisClass::HandleAttachToBack);
		AttachTask->ReadyForActivation();
		
		UAbilityTask_PlayMontageAndWait* MontageTask =UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,TEXT("UnequipSwordMontage"),UnequipMontage);

		MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageCancelled);
		MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageCancelled);
		MontageTask->ReadyForActivation();
		return;
	}

	if (!AttachSwordToBack())
	{
		FinishUnequip(true);
		return;
	}

	FinishUnequip(false);
}

void UFirstGA_DKUnequipSword::HandleAttachToBack(FGameplayEventData Payload)
{
	if (!AttachSwordToBack())
	{
		FinishUnequip(true);
	}
}

bool UFirstGA_DKUnequipSword::AttachSwordToBack()
{
	if (bSwordAttachedToBack)
	{
		return true;
	}

	ADKCharacter* DKCharacter = GetDKCharacterFromActorInfo();
	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();

	// Notify 到来前，剑仍在手中，仍是“当前已装备武器”。
	ADKWeapon* Weapon = CombatComponent? CombatComponent->GetDKCurrentEquippedWeapon(): nullptr;

	if (!DKCharacter || !CombatComponent || !ASC || !Weapon)
	{
		return false;
	}

	const FName BackSocket = Weapon->DKWeaponData.UnequippedSocketName;

	// 先完成画面 Attach；失败时保持原装备状态，避免逻辑和画面不一致。
	if (!DKCharacter->GetMesh()->DoesSocketExist(BackSocket) ||!Weapon->AttachToComponent(
		DKCharacter->GetMesh(),FAttachmentTransformRules::SnapToTargetNotIncludingScale,BackSocket))
	{
		UE_LOG(LogTemp, Error,TEXT("Sword back socket is invalid: %s"),*BackSocket.ToString());
		return false;
	}

	CombatComponent->ToggleWeaponCollision(false);

	TArray<FGameplayAbilitySpecHandle> GrantedHandles =Weapon->GetGrantedAbilitySpecHandles();

	ASC->RemoveGrantedWeaponAbilities(GrantedHandles);
	Weapon->ClearGrantedAbilitySpecHandles();

	CombatComponent->CurrentEquippedWeaponTag = FGameplayTag();

	bSwordAttachedToBack = true;
	return true;
}

void UFirstGA_DKUnequipSword::HandleMontageCompleted()
{
	FinishUnequip(!bSwordAttachedToBack);
}

void UFirstGA_DKUnequipSword::HandleMontageCancelled()
{
	FinishUnequip(!bSwordAttachedToBack);
}

void UFirstGA_DKUnequipSword::FinishUnequip(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,bWasCancelled);
}
