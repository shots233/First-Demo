// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DataAsset_StartUpDataBase.generated.h"

/**
 * 
 */

class UGameplayEffect;
class UGameplayAbility;
class UFirstAbilitySystemComponent;

UCLASS()
class FIRST_API UDataAsset_StartUpDataBase : public UDataAsset
{
	GENERATED_BODY()
	
public:
	// 作用：将数据资产中的初始 Effect 和 Ability 整体交给指定 ASC，是角色出生配置的总入口。
	virtual void GiveToAbilitySystemComponent(UFirstAbilitySystemComponent* InASCToGive, int32 ApplyLevel = 1);
	
protected:
	// 这组能力授予后会在 OnGiveAbility 内立即激活，适合生成初始武器等一次性动作。
	UPROPERTY(EditDefaultsOnly, Category="StartupData")
	TArray<TSubclassOf<UGameplayAbility>> ActivateOnGivenAbilities;
	
	// 预留给由 GameplayEvent 等外部事件触发的 Ability；本阶段可以先不填。
	UPROPERTY(EditDefaultsOnly, Category="StartupData")
	TArray<TSubclassOf<UGameplayAbility>> ReactiveAbilities;
	
	// 初始属性 Effect 应使用 Instant，角色出生时只应用一次即可。
	UPROPERTY(EditDefaultsOnly, Category="StartupData")
	TArray<TSubclassOf<UGameplayEffect>> StartUpGameplayEffects;
	
	// 作用：把一组 Ability 类转为 AbilitySpec 并授予 ASC；没有输入 Tag 的能力适合自动或事件触发。
	void GrantAbilities(const TArray<TSubclassOf<UGameplayAbility>>& InAbilitiesToGive, 
		UFirstAbilitySystemComponent* InASCToGive,int32 ApplyLevel);
	
	
};
