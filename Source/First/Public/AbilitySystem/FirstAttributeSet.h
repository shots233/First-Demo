// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "FirstAttributeSet.generated.h"

/**
 * 
 */
 
 // 这四个 GAS 宏生成 Attribute 描述、读取、设置、初始化函数，
 // 例如 GetHealthAttribute / GetHealth / SetHealth / InitHealth。
 #define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
 	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
 	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
 	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
 	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)
 
UCLASS()
class FIRST_API UFirstAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
public:
	UFirstAttributeSet();
	
	// 作用：每次 GameplayEffect 修改完属性后统一处理夹紧、伤害转生命、死亡 Tag 等后续逻辑。
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;
	
	// FGameplayAttributeData 不是普通 float；它带有 GAS 的聚合与复制语义。
	UPROPERTY(BlueprintReadOnly, Category="Health")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UFirstAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category="Health")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UFirstAttributeSet, MaxHealth)

	//体力
	UPROPERTY(BlueprintReadOnly, Category="Stamina")
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS(UFirstAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, Category="Stamina")
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS(UFirstAttributeSet, MaxStamina)

	//攻击力
	UPROPERTY(BlueprintReadOnly, Category="Damage")
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS(UFirstAttributeSet, AttackPower)

	//防御力
	UPROPERTY(BlueprintReadOnly, Category="Damage")
	FGameplayAttributeData DefensePower;
	ATTRIBUTE_ACCESSORS(UFirstAttributeSet, DefensePower)

	// Meta 属性：只承接本次伤害结果，不作为长期数值显示。
	UPROPERTY(BlueprintReadOnly, Category="Damage")
	FGameplayAttributeData DamageTaken;
	ATTRIBUTE_ACCESSORS(UFirstAttributeSet, DamageTaken)
	
	
};
