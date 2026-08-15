// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimInstances/FirstBaseAnimInstance.h"
#include "FirstDKAnimInstance.generated.h"

class ABaseCharacter;
class UCharacterMovementComponent;
/**
 * 
 */
UCLASS()
class FIRST_API UFirstDKAnimInstance : public UFirstBaseAnimInstance
{
	GENERATED_BODY()
	
public:
	// 动画实例建初始化
	virtual void NativeInitializeAnimation() override;
	
	// 动画更新时调用。ThreadSafe 版本适合计算简单、只读的动画数据。  也是本地线程安全更新动画
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;
	
protected:
	// 当前动画实例所属的角色。ABaseCharacter 能同时服务 Hero 和 Enemy。
	UPROPERTY()
	TObjectPtr<ABaseCharacter> OwningCharacter = nullptr;
	
	// ACharacter 自带的移动组件，用来读取加速度、速度等移动状态。
	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> OwningMovementComponent = nullptr;
	
	// 水平移动速度，给 Blend Space 的 GroundSpeed 轴使用。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="AnimData|Locomotion")
	float GroundSpeed = 0.f;
	
	// 当前是否有移动输入造成的加速度。以后可用于 Idle 与 Start/Stop 过渡。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="AnimData|Locomotion")
	bool bHasAcceleration = false;
	
	// 是否应该进入移动表现。给状态机过渡使用，比直接写 GroundSpeed > 0 更好读。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="AnimData|Locomotion")
	bool bShouldMove = false;
	
};
