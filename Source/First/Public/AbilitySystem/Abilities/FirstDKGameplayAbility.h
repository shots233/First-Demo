// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstGameplayAbility.h"
#include "FirstDKGameplayAbility.generated.h"

class UDKCombatComponent;
class ADKCharacter;
class UGameplayEffect;
/**
 * 
 */
UCLASS()
class FIRST_API UFirstDKGameplayAbility : public UFirstGameplayAbility
{
	GENERATED_BODY()
public:
	// 作用：取得当前 Ability 所属的 DK 角色，并缓存 Cast 结果以减少重复查找。
	ADKCharacter* GetDKCharacterFromActorInfo();
	
	// 作用：取得 DK 的专用战斗组件，用于读取当前武器和发送命中相关操作。
	UDKCombatComponent* GetDKCombatComponentFromActorInfo();

	// 作用：创建一次 DK 近战伤害的 EffectSpec，并把武器伤害和连击段数写入 SetByCaller。
	FGameplayEffectSpecHandle MakeDKDamageEffectSpecHandle(TSubclassOf<UGameplayEffect> EffectClass,float InWeaponBaseDamage,int32 InComboCount);

private:
	TWeakObjectPtr<ADKCharacter> CachedDKCharacter;
	
};
