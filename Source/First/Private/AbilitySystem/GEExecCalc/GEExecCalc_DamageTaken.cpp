// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GEExecCalc/GEExecCalc_DamageTaken.h"

#include "MyGameplayTags.h"
#include "AbilitySystem/FirstAttributeSet.h"

struct FFirstDamageCapture
{
	DECLARE_ATTRIBUTE_CAPTUREDEF(AttackPower)
	DECLARE_ATTRIBUTE_CAPTUREDEF(DefensePower)
	DECLARE_ATTRIBUTE_CAPTUREDEF(DamageTaken)

	// 作用：定义本公式要从 Source 与 Target 捕获哪些属性，以及这些属性是否实时取值。
	FFirstDamageCapture()
	{
		// false 表示在 Effect 执行时取当前值，而不是在 Spec 创建时冻结快照。
		// Source 是攻击者，Target 是受击者；DamageTaken 是写入目标的 Meta 属性。
		DEFINE_ATTRIBUTE_CAPTUREDEF(UFirstAttributeSet, AttackPower, Source, false)
		DEFINE_ATTRIBUTE_CAPTUREDEF(UFirstAttributeSet, DefensePower, Target, false)
		DEFINE_ATTRIBUTE_CAPTUREDEF(UFirstAttributeSet, DamageTaken, Target, false)
		}
};

// 作用：返回全局唯一的属性捕获定义，避免每次伤害计算都重新创建 CaptureDef。
static const FFirstDamageCapture& GetFirstDamageCapture()
{
	static FFirstDamageCapture DamageCapture;
	return DamageCapture;
}

UGEExecCalc_DamageTaken::UGEExecCalc_DamageTaken()
{
	// 只有注册到 RelevantAttributesToCapture 的属性，Execute 中才可被安全捕获。
	RelevantAttributesToCapture.Add(GetFirstDamageCapture().AttackPowerDef);
	RelevantAttributesToCapture.Add(GetFirstDamageCapture().DefensePowerDef);
	RelevantAttributesToCapture.Add(GetFirstDamageCapture().DamageTakenDef);
}



void UGEExecCalc_DamageTaken::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	Super::Execute_Implementation(ExecutionParams, OutExecutionOutput);
	
	const FGameplayEffectSpec& EffectSpec = ExecutionParams.GetOwningSpec();
	
	// 把来源/目标 Tag 带入聚合计算，后续可据此实现“无敌”“暴击”“火焰易伤”等 Tag 修正。
	FAggregatorEvaluateParameters EvaluateParameters;
	EvaluateParameters.SourceTags = EffectSpec.CapturedSourceTags.GetAggregatedTags();
	EvaluateParameters.TargetTags = EffectSpec.CapturedTargetTags.GetAggregatedTags();
	
	float SourceAttackPower = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(GetFirstDamageCapture().AttackPowerDef, EvaluateParameters, SourceAttackPower);
	
	float TargetDefensePower = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(GetFirstDamageCapture().DefensePowerDef,EvaluateParameters,TargetDefensePower);
	
	// SetByCaller 由 MakeDKDamageEffectSpecHandle 在攻击时写入；
	// 默认值 0 可以防止漏配置时产生随机伤害，但也会让本次攻击不扣血。
	const float BaseDamage = EffectSpec.GetSetByCallerMagnitude(MyGameplayTags::DK_SetByCaller_BaseDamage, false, 0.f);
	
	const float ComboCount = EffectSpec.GetSetByCallerMagnitude(MyGameplayTags::DK_SetByCaller_ComboCount,false,1.f);
	
	// 公式：基础伤害 x 连击加成 x 攻击力 / 防御力。
	// 第 1/2/3 段的倍率分别是 1.00 / 1.15 / 1.30，便于肉眼验证。
	const float ComboMultiplier = 1.f + FMath::Max(ComboCount -1.f, 0.f) * 0.15f;
	const float safeDefemse = FMath::Max(TargetDefensePower, 1.f);
	const float FinalDamageDone = BaseDamage * ComboMultiplier * SourceAttackPower / safeDefemse;
	if (FinalDamageDone > 0.f)
	{
		// DamageTaken 每次都会在 AttributeSet 中清零，因此 Override 能表达“本次最终伤害”。
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
			GetFirstDamageCapture().DamageTakenProperty, EGameplayModOp::Override, FinalDamageDone));
	}
}
