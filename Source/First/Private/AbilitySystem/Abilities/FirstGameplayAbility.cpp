// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/FirstGameplayAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Components/Combat/FirstCombatComponent.h"

void UFirstGameplayAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);
	// GiveAbility 只是把 Ability 放进 ASC；这里额外 TryActivate 才实现“出生即执行”。
	if (AbilityActivationPolicy == EFirstAbilityActivationPolicy::OnGiven && ActorInfo && !Spec.IsActive())
	{
		ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle, false);
	}
}

void UFirstGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	
	
	// SpawnSword 执行完便删除自己的 Spec，避免它留在可激活列表中被误用。
	// 装备、闪避、攻击不能使用此策略，它们需要长期保留在 ASC 中。
	if (AbilityActivationPolicy == EFirstAbilityActivationPolicy::OnGiven && ActorInfo)
	{
		ActorInfo->AbilitySystemComponent->ClearAbility(Handle);
	}
	
}

UFirstAbilitySystemComponent* UFirstGameplayAbility::GetFirstAbilitySystemComponentFromActorInfo() const
{
	// CurrentActorInfo 只在 Ability 正确激活期间有效；因此不要在构造函数中调用它。
	return Cast<UFirstAbilitySystemComponent>(CurrentActorInfo->AbilitySystemComponent);
}

UFirstCombatComponent* UFirstGameplayAbility::GetFirstCombatComponentFromActorInfo() const
{
	return GetAvatarActorFromActorInfo()? GetAvatarActorFromActorInfo()->FindComponentByClass<UFirstCombatComponent>(): nullptr;
}

FActiveGameplayEffectHandle UFirstGameplayAbility::ApplyEffectSpecHandleToTarget(AActor* TargetActor,
	const FGameplayEffectSpecHandle& InSpecHandle)
{
	// 目标只需实现 IAbilitySystemInterface，不需要写死为 AEnemyCharacter，
	// 这样以后其他可受击角色也能复用这条伤害路径。
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);

	if (!TargetASC || !InSpecHandle.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}
	
	// Source ASC 负责把带有攻击者上下文的 Spec 应用到 Target ASC。
	return GetFirstAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToTarget(*InSpecHandle.Data, TargetASC);
}
