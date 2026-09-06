// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstGameplayAbility.h"
#include "Types/FirstCombatTypes.h"
#include "FirstBossGameplayAbility.generated.h"

struct FFirstMeleeDefenseData;
class ABossCharacter;
class UBossCombatComponent;
class UGameplayEffect;
/**
 * BOSS 动作/攻击能力的公共基类。
 * 派生类在构造函数里设置 CooldownDuration，并把自己的身份标签加入 AbilityTags 与 AssetTags。
 */
UCLASS()
class FIRST_API UFirstBossGameplayAbility : public UFirstGameplayAbility
{
	GENERATED_BODY()
public:
	// 本次招式的冷却秒数（派生类构造函数里设置）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Cooldown")
	float CooldownDuration = 3.f;

	ABossCharacter* GetBossCharacterFromActorInfo();
	UBossCombatComponent* GetBossCombatComponentFromActorInfo();

	// 生成一次 BOSS 近战伤害 Spec（复用玩家伤害 GE 与 ExecCalc）。
	FGameplayEffectSpecHandle MakeBossDamageEffectSpecHandle(TSubclassOf<UGameplayEffect> EffectClass,float InBaseDamage);
	// 返回 Damaged 时派生攻击继续应用生命伤害；其它结果已经被消费。
	EFirstDefenseResult ResolveTargetDefense(AActor* TargetActor,const FFirstMeleeDefenseData& AttackData) const;
protected:
	// 派生攻击在 ResolveTargetDefense 返回 Damaged 后调用：
	// 播放 BP_Boss 上配置的"命中玩家"音效与血花（从被击者位置发出）。
	void PlayBossHitPlayerFeedback(const AActor* TargetActor);

	// 用 CooldownDuration 写入 SetByCaller 后应用冷却 GE。
	virtual void ApplyCooldown(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;
};
