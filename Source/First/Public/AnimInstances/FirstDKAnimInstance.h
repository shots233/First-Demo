// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimInstances/FirstBaseAnimInstance.h"
#include "FirstDKAnimInstance.generated.h"

class ABaseCharacter;
class UCharacterMovementComponent;
class ADKCharacter;
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

	// 实际移动方向相对角色朝向的前后分量：+1 前，-1 后。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly,
		Category="AnimData|Locomotion")
	float LocalForwardDirection = 0.f;

	// 实际移动方向相对角色朝向的左右分量：+1 右，-1 左。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly,
		Category="AnimData|Locomotion")
	float LocalRightDirection = 0.f;

	// 给 Walk Blend Space Player 使用，125 速度约为 0.5，250 速度约为 1.0。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly,
		Category="AnimData|Locomotion")
	float WalkAnimationPlayRate = 1.f;

	// DKM Walk 动画按 250 作为首版参考速度；可在 AnimBP 类默认值中微调。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category="AnimData|Locomotion",
		meta=(ClampMin="1.0"))
	float WalkReferenceSpeed = 250.f;
	
	// 当前是否处于目标锁定。AnimBP 用它切换到“锁定定向移动”分支。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="AnimData|Locomotion")
	bool bIsTargetLocked = false;

	// 锁定或格挡时，角色朝向与移动输入解耦，需要使用八向移动动画。
	// 该值只控制动画分支，不会伪造目标锁定状态。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="AnimData|Locomotion")
	bool bUseDirectionalLocomotion = false;

	// 移动输入在角色本地坐标系的分量（-1..1）：
	// MoveForward：沿角色朝前为正；MoveRight：沿角色右侧为正。
	// AnimBP 的 2D BlendSpace 用它取方向动画。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="AnimData|Locomotion")
	float MoveForward = 0.f;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="AnimData|Locomotion")
	float MoveRight = 0.f;
	
};
