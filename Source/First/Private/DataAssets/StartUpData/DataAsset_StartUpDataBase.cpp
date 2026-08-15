// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAssets/StartUpData/DataAsset_StartUpDataBase.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"

void UDataAsset_StartUpDataBase::GiveToAbilitySystemComponent(UFirstAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
	check(InASCToGive);
	
	// 先建立属性，再授予可能在 OnGiveAbility 中立刻启动的技能。
	// 当前 SpawnSword 虽然不读取数值，但这个顺序能保证以后任何 OnGiven Ability
	// 在 ActivateAbility 中看到的 Health、Stamina 都已经是正式出生值，而不是构造默认值 1。
	
	for (const TSubclassOf<UGameplayEffect>& EffectClass : StartUpGameplayEffects)
	{
		if (!EffectClass)
		{
			continue;
		}
		
		// GE 类的 CDO 是稳定的模板；Apply 时 ASC 会为本次应用创建实际的 EffectSpec。
		const UGameplayEffect* EffectCDO = EffectClass->GetDefaultObject<UGameplayEffect>();
		InASCToGive->ApplyGameplayEffectToSelf(EffectCDO, ApplyLevel, InASCToGive->MakeEffectContext());
		
	}
	
	// Effect 全部应用完后再授予 Ability。OnGiven 类型会在 GiveAbility 期间立即尝试激活。
	GrantAbilities(ActivateOnGivenAbilities, InASCToGive, ApplyLevel);
	GrantAbilities(ReactiveAbilities, InASCToGive, ApplyLevel);
}

void UDataAsset_StartUpDataBase::GrantAbilities(const TArray<TSubclassOf<UGameplayAbility>>& InAbilitiesToGive,
	UFirstAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
	for(const TSubclassOf<UGameplayAbility>& AbilityClass : InAbilitiesToGive)
	{
		if (!AbilityClass)
		{
			continue;
		}
		
		// 这里没有输入 Tag：这批能力要么 OnGiven 自动执行，要么以后由事件触发。
		FGameplayAbilitySpec AbilitySpec(AbilityClass);
		AbilitySpec.SourceObject = InASCToGive->GetAvatarActor();
		AbilitySpec.Level = ApplyLevel;
		
		InASCToGive->GiveAbility(AbilitySpec);
		
	}
}
