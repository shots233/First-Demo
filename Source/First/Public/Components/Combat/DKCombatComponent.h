// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Combat/FirstCombatComponent.h"
#include "DKCombatComponent.generated.h"

class ADKWeapon;
class UParticleSystem;
class USoundBase;
/**
 * 
 */
UCLASS()
class FIRST_API UDKCombatComponent : public UFirstCombatComponent
{
	GENERATED_BODY()
	
public:
	// 作用：按 Tag 取得 DK 专用武器类型，避免调用方反复手动 Cast。
	UFUNCTION(BlueprintCallable, Category="First | Combat")
	ADKWeapon* GetDKCarriedWeaponByTag(FGameplayTag InWeaponTag) const;
	
	// 作用：取得当前装备的 DK 武器；未装备或类型不匹配时返回 nullptr。
	UFUNCTION(BlueprintCallable, Category="First | Combat")
	ADKWeapon* GetDKCurrentEquippedWeapon() const;
	
	// 作用：读取当前武器在指定 Ability Level 下的基础伤害，供攻击 Ability 制作 Damage Spec。
	UFUNCTION(BlueprintCallable, Category="First | Combat")
	float GetDKCurrentEquippedWeaponDamageAtLevel(float InLevel) const;

	// 读取当前武器在指定 Ability Level 下的削韧值。
	UFUNCTION(BlueprintCallable, Category="First | Combat")
	float GetDKCurrentEquippedWeaponPoiseDamageAtLevel(
		float InLevel) const;
	
	// 作用：把一次武器命中去重后发送为 DK.Event.MeleeHit，交给攻击 Ability 结算伤害。
	virtual void OnHitTargetActor(AActor* HitActor) override;

	// 玩家武器命中目标的结果音；从被击目标位置发出。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|SFX",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<USoundBase> HitSound;

	// 在被击目标位置播放一次 3D 命中音；Sound 为空时静默。
	void PlayHitTargetSFX(AActor* HitActor) const;

	// 在被击目标躯干位置生成一次性血花；血花资源按受击者组织：
	// 从被击目标的 BP 上读取（ABossCharacter 的 BloodVFX，Boss|Feedback 类别）。
	void PlayHitTargetVFX(AActor* HitActor) const;
};
