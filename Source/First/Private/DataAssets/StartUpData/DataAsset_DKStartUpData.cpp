// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAssets/StartUpData/DataAsset_DKStartUpData.h"

#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Types/FirstStructTypes.h"

void UDataAsset_DKStartUpData::GiveToAbilitySystemComponent(UFirstAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
	Super::GiveToAbilitySystemComponent(InASCToGive, ApplyLevel);
	
	for (const FFirstDKAbilitySet& AbilitySet : DKStartUpAbilitySets)
	{
		if (!AbilitySet.IsValid())
		{
			continue;
		}
		
		FGameplayAbilitySpec AbilitySpec(AbilitySet.AbilityToGrant);
		AbilitySpec.SourceObject = InASCToGive->GetAvatarActor();
		AbilitySpec.Level = ApplyLevel;
		// 只有这批“角色固有技能”在数据资产中绑定输入 Tag。
		AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilitySet.InputTag);
		
		InASCToGive->GiveAbility(AbilitySpec);
	}
}
