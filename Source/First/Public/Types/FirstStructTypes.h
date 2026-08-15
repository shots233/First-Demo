#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ScalableFloat.h"
#include "Abilities/GameplayAbility.h"
#include "FirstStructTypes.generated.h"

class UInputMappingContext;

USTRUCT(BlueprintType)
struct FFirstDKAbilitySet
{
	GENERATED_BODY()
	
	// Categories 只影响编辑器选择器：它约束此处只能挑选 InputTag.*，避免误填状态 Tag。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(Categories="InputTag"))
	FGameplayTag InputTag;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UGameplayAbility> AbilityToGrant;	
	
	// 作用：检查这一条“输入 Tag -> Ability”映射是否填写完整，供授予 Ability 前做安全过滤。
	bool IsValid() const
	{
		// 在 C++ 授予前先过滤未配置完整的数据行，避免空 Ability 造成隐蔽问题。
		return InputTag.IsValid() && AbilityToGrant;
	}
};

USTRUCT(BlueprintType)
struct FFirstDKWeaponData
{
	GENERATED_BODY()
	
	// 本阶段可先留空。它为“不同武器切换不同输入映射”预留，
	// 当前攻击输入统一在 IMC_Default 中配置，因此暂时不会由 C++ 自动添加它。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UInputMappingContext> WeaponInputMappingContext;
	
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(TitleProperty="InputTag"))
	TArray<FFirstDKAbilitySet> DefaultWeaponAbilities;
	
	// 用 ScalableFloat 而非 float，是为 Ability Level 或以后数据表成长预留。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FScalableFloat WeaponBaseDamage;
	
	// 装备和卸下只改变同一个武器 Actor 的挂点；Socket 名称属于武器配置，不写死在 Ability 流程里。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FName EquippedSocketName = TEXT("Weapon_R");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FName UnequippedSocketName = TEXT("Weapon_B_A");

	// Montage 是表现资源。C++ Ability 决定何时播放，武器数据只回答“播放哪一个”。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UAnimMontage> EquipMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UAnimMontage> UnequipMontage;

	// 数组下标 0/1/2 对应第 1/2/3 段。数组让 C++ 连击状态机不依赖固定的三个变量名。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<TObjectPtr<UAnimMontage>> LightAttackMontages;
};
