// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GameplayEffects/FirstGE_Damage.h"

#include "AbilitySystem/GEExecCalc/GEExecCalc_DamageTaken.h"

UFirstGE_Damage::UFirstGE_Damage()
{
	// 伤害是一次性结算，不需要在目标身上保留持续中的 ActiveGameplayEffect。
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayEffectExecutionDefinition DamageExecution;
	DamageExecution.CalculationClass = UGEExecCalc_DamageTaken::StaticClass();
	Executions.Add(DamageExecution);
}
