// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKSpawnSword.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UFirstGA_DKSpawnSword : public UFirstDKGameplayAbility
{
	GENERATED_BODY()
public:
	// 作用：设置出生即执行策略和 Ability 身份 Tag。
	UFirstGA_DKSpawnSword();
protected:
	// 作用：生成默认剑、挂到背部 Socket，并注册到 DKCombatComponent。
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData) override;
};
