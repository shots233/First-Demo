// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "FirstGameplayAbility.generated.h"

class UFirstCombatComponent;
class UFirstAbilitySystemComponent;

enum class EFirstAbilityActivationPolicy : uint8
{
	// 正常输入/事件触发的 Ability，例如装备、闪避、攻击。
	OnTriggered,
	// 授予后只执行一次的 Ability，例如角色出生时生成剑。
	OnGiven
};

/**
 * 
 */
UCLASS()
class FIRST_API UFirstGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()
protected:
	// 作用：AbilitySpec 被授予 ASC 时，根据 ActivationPolicy 决定是否立刻启动该 Ability。
	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	
	// 作用：Ability 结束时执行基类清理；若它是一次性 OnGiven Ability，则移除自己的 Spec。
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	
	// 由具体 C++ Ability 的构造函数设置，不再依赖 Ability 蓝图默认值。
	EFirstAbilityActivationPolicy AbilityActivationPolicy = EFirstAbilityActivationPolicy::OnTriggered;
	
	// 作用：从当前 Ability 的 ActorInfo 取得项目自定义 ASC，供原生 Ability 调用 GAS 功能。
	UFirstAbilitySystemComponent* GetFirstAbilitySystemComponentFromActorInfo() const;
	
	// 作用：从当前 AvatarActor 查找战斗组件，让 Ability 不需要知道角色的具体类名。
	UFirstCombatComponent* GetFirstCombatComponentFromActorInfo() const;
	
	// 作用：将已经制作好的 EffectSpec 应用给目标 Actor，并返回本次应用的运行时 Handle。
	FActiveGameplayEffectHandle ApplyEffectSpecHandleToTarget(AActor* TargetActor,const FGameplayEffectSpecHandle& InSpecHandle);
	
};
