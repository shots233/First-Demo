// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/FirstAbilitySystemComponent.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "MyGameplayTags.h"
#include "Types/FirstStructTypes.h"
#include "Abilities/GameplayAbility.h"


void UFirstAbilitySystemComponent::StartDodgeStaminaRecovery()
{
	if (bEndingPlay || bOwnsDodgeExhaustedTag)
	{
		return;
	}

	const UFirstAttributeSet* Attributes = GetSet<UFirstAttributeSet>();
	if (!Attributes || Attributes->GetStamina() > 0.f)
	{
		return;
	}

	// 先记录所有权并监听，再加 Tag：Tag 回调引起的属性变化也能解除恢复锁。
	bOwnsDodgeExhaustedTag = true;
	DodgeRecoveryStaminaChangedHandle = GetGameplayAttributeValueChangeDelegate(
		UFirstAttributeSet::GetStaminaAttribute()).AddUObject(
			this, &ThisClass::HandleDodgeRecoveryAttributeChanged);
	DodgeRecoveryMaxStaminaChangedHandle = GetGameplayAttributeValueChangeDelegate(
		UFirstAttributeSet::GetMaxStaminaAttribute()).AddUObject(
			this, &ThisClass::HandleDodgeRecoveryAttributeChanged);
	AddLooseGameplayTag(MyGameplayTags::DK_Status_DodgeExhausted);
	TryFinishDodgeStaminaRecovery();
}

void UFirstAbilitySystemComponent::HandleDodgeRecoveryAttributeChanged(const FOnAttributeChangeData& Data)
{
	TryFinishDodgeStaminaRecovery();
}

void UFirstAbilitySystemComponent::TryFinishDodgeStaminaRecovery()
{
	if (!bOwnsDodgeExhaustedTag || bEndingPlay)
	{
		return;
	}

	const UFirstAttributeSet* Attributes = GetSet<UFirstAttributeSet>();
	if (Attributes && Attributes->GetMaxStamina() > 0.f &&
		Attributes->GetStamina() >= Attributes->GetMaxStamina())
	{
		ClearDodgeStaminaRecovery();
	}
}

void UFirstAbilitySystemComponent::ClearDodgeStaminaRecovery()
{
	const bool bRemoveOwnedTag = bOwnsDodgeExhaustedTag;
	bOwnsDodgeExhaustedTag = false;

	if (DodgeRecoveryStaminaChangedHandle.IsValid())
	{
		GetGameplayAttributeValueChangeDelegate(UFirstAttributeSet::GetStaminaAttribute())
			.Remove(DodgeRecoveryStaminaChangedHandle);
		DodgeRecoveryStaminaChangedHandle.Reset();
	}
	if (DodgeRecoveryMaxStaminaChangedHandle.IsValid())
	{
		GetGameplayAttributeValueChangeDelegate(UFirstAttributeSet::GetMaxStaminaAttribute())
			.Remove(DodgeRecoveryMaxStaminaChangedHandle);
		DodgeRecoveryMaxStaminaChangedHandle.Reset();
	}

	// 状态先清干净再移除自有的一层 Tag，避免 Tag 回调重入覆盖新状态。
	if (bRemoveOwnedTag)
	{
		RemoveLooseGameplayTag(MyGameplayTags::DK_Status_DodgeExhausted);
	}
}

void UFirstAbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	ClearDodgeStaminaRecovery();
	Super::EndPlay(EndPlayReason);
}

void UFirstAbilitySystemComponent::OnAbilityInputPressed(const FGameplayTag& InInputTag)
{
	if (!InInputTag.IsValid())
	{
		return;
	}

	TArray<FGameplayAbilitySpecHandle> HandlesToProcess;

	// 第一轮只收集 Handle；此时不激活 Ability，不会修改数组。
	for (const FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InInputTag))
		{
			HandlesToProcess.Add(AbilitySpec.Handle);
		}
	}

	// 第二轮才发送输入和激活；装备剑期间即使 GiveAbility，也不会破坏遍历。
	for (const FGameplayAbilitySpecHandle& Handle : HandlesToProcess)
	{
		FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(Handle);
		if (!AbilitySpec)
		{
			continue;
		}

		// 先标记当前 Spec 处于按下状态。
		// 首次按下会用于激活 Ability；已激活时则供输入任务查询。
		AbilitySpec->InputPressed = true;

		if (AbilitySpec->IsActive())
		{
			// 已激活的 Ability：把普通输入通知给 Ability 实例。
			AbilitySpecInputPressed(*AbilitySpec);

			// WaitInputPress 不只依赖 AbilitySpecInputPressed。
			// 它还监听 GAS 的通用 InputPressed 事件；这一步与 UE 原生
			// AbilityLocalInputPressed 的行为保持一致。
			TArray<UGameplayAbility*> AbilityInstances =AbilitySpec->GetAbilityInstances();

			const FGameplayAbilityActivationInfo& ActivationInfo =
				AbilityInstances.IsEmpty()? AbilitySpec->ActivationInfo: AbilityInstances.Last()->GetCurrentActivationInfoRef();

			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed,
				AbilitySpec->Handle,ActivationInfo.GetActivationPredictionKey());
		}
		else
		{
			// 首次按下：该 Ability 尚未运行，尝试激活它。
			// 因为 Handle 已在第一轮收集完毕，所以即使 GiveAbility 修改数组，
			// 也不会重新出现“遍历期间数组改变”的问题。
			TryActivateAbility(Handle);
		}
	}
}

void UFirstAbilitySystemComponent::OnAbilityInputReleased(const FGameplayTag& InInputTag)
{
	if (!InInputTag.IsValid())
	{
		return;
	}

	TArray<FGameplayAbilitySpecHandle> HandlesToProcess;

	// 与 Pressed 相同，先收集 Handle，避免回调期间可激活列表发生变化。
	for (const FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InInputTag))
		{
			HandlesToProcess.Add(AbilitySpec.Handle);
		}
	}

	for (const FGameplayAbilitySpecHandle& Handle : HandlesToProcess)
	{
		FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(Handle);
		if (!AbilitySpec)
		{
			continue;
		}

		AbilitySpec->InputPressed = false;

		if (!AbilitySpec->IsActive())
		{
			continue;
		}

		AbilitySpecInputReleased(*AbilitySpec);

		const TArray<UGameplayAbility*> AbilityInstances =AbilitySpec->GetAbilityInstances();

		const FGameplayAbilityActivationInfo& ActivationInfo =AbilityInstances.IsEmpty()
		? AbilitySpec->ActivationInfo
		: AbilityInstances.Last()->GetCurrentActivationInfoRef();

		InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased,AbilitySpec->Handle,ActivationInfo.GetActivationPredictionKey());
	}
}

// 作用：在装备武器时创建 AbilitySpec、绑定输入 Tag，并记录每个 Spec 的 Handle。
void UFirstAbilitySystemComponent::GrantWeaponAbilities(const TArray<FFirstDKAbilitySet>& InWeaponAbilities,
	int32 ApplyLevel, TArray<FGameplayAbilitySpecHandle>& OutGrantedAbilitySpecHandles)
{
	for (const FFirstDKAbilitySet& AbilitySet : InWeaponAbilities)
	{
		if (!AbilitySet.IsValid())
		{
			continue;
		}
		
		FGameplayAbilitySpec AbilitySpec(AbilitySet.AbilityToGrant);
		AbilitySpec.SourceObject = GetAvatarActor();
		AbilitySpec.Level = ApplyLevel;
		AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilitySet.InputTag);
		
		OutGrantedAbilitySpecHandles.AddUnique(GiveAbility(AbilitySpec));
		
		
	}
}

void UFirstAbilitySystemComponent::RemoveGrantedWeaponAbilities(
	TArray<FGameplayAbilitySpecHandle>& InSpecHandlesToRemove)
{
	for (const FGameplayAbilitySpecHandle& SpecHandle : InSpecHandlesToRemove)
	{
		if (SpecHandle.IsValid())
		{
			// 只移除这把武器曾经授予的 Spec，不影响闪避、装备等角色固有 Ability。
			ClearAbility(SpecHandle);
		}
	}
	
	InSpecHandlesToRemove.Empty();
}
