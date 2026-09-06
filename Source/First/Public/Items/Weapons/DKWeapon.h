// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Items/Weapons/FirstWeaponBase.h"
#include "Types/FirstStructTypes.h"
#include "DKWeapon.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API ADKWeapon : public AFirstWeaponBase
{
	GENERATED_BODY()
public:
	// 这把剑自己的输入映射、攻击 Ability 和基础伤害，不放在角色上。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="WeaponData")
	FFirstDKWeaponData DKWeaponData;
	
	// 作用：保存本次装备操作实际授予的 AbilitySpecHandle，作为卸下武器时的删除清单。
	UFUNCTION(BlueprintCallable)
	void AssignGrantedAbilitySpecHandles(const TArray<FGameplayAbilitySpecHandle>& InSpecHandles);
	
	// 作用：返回这把武器当前保存的 Ability Handle 列表，供卸下 Ability 精确移除。
	UFUNCTION(BlueprintPure)
	TArray<FGameplayAbilitySpecHandle> GetGrantedAbilitySpecHandles() const;
	
	// 作用：卸下完成后清除武器保存的旧 Handle，避免调试时误以为这些 Spec 仍然有效。
	void ClearGrantedAbilitySpecHandles();

private:
	// 只保存“本武器在本次装备时授予”的 Handle；卸下时据此精确移除。
	UPROPERTY()
	TArray<FGameplayAbilitySpecHandle> GrantedAbilitySpecHandles;
};
