// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "GEExecCalc_DamageTaken.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UGEExecCalc_DamageTaken : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()
public:
	// 作用：注册本伤害公式需要捕获的属性定义，让 Execute_Implementation 可读取攻防和写入 DamageTaken。
	UGEExecCalc_DamageTaken();
	
	// 作用：每次伤害 GameplayEffect 应用时计算最终伤害，并把结果输出为目标 DamageTaken 的修改量。
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
	
};
