// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Types/FirstStructTypes.h"
#include "FirstAbilitySystemComponent.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UFirstAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
	
public:
	// 接收角色按下某个 Ability 输入后的 Tag：通知已运行技能，并尝试启动尚未运行的匹配技能。
	void OnAbilityInputPressed(const FGameplayTag& InInputTag);
	// 接收角色松开按键后的 Tag：让需要监听松开事件的 AbilityTask 得到通知。
	void OnAbilityInputReleased(const FGameplayTag& InInputTag);
	
	// 根据武器数据资产授予一批 Ability，并把运行时 Handle 输出给调用方，供卸下武器时移除。
	UFUNCTION(BlueprintCallable, Category="First|AbilityS")
	void GrantWeaponAbilities(const TArray<FFirstDKAbilitySet>& InWeaponAbilities, int32 ApplyLevel, TArray<FGameplayAbilitySpecHandle>& OutGrantedAbilitySpecHandles );
	
	UFUNCTION(BlueprintCallable, Category="First|AbilityS")
	void RemoveGrantedWeaponAbilities(UPARAM(ref) TArray<FGameplayAbilitySpecHandle>& InSpecHandlesToRemove);
	
};
