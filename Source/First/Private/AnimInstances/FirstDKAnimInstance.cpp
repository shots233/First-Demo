// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstances/FirstDKAnimInstance.h"

#include "Character/BaseCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

void UFirstDKAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	
	// TryGetPawnOwner() 返回拥有这个 Mesh 动画实例的 Pawn。
	OwningCharacter = Cast<ABaseCharacter>(TryGetPawnOwner());
	
	//获取Pawn的移动组件
	if (OwningCharacter)
	{
		OwningMovementComponent = OwningCharacter->GetCharacterMovement();
	}
}

void UFirstDKAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	
	if (!OwningCharacter || !OwningMovementComponent)
	{
		return;
	}
	
	// Size2D 只计算 XY 平面速度，避免跳跃/下落的 Z 速度污染地面移动动画。
	GroundSpeed = OwningCharacter->GetVelocity().Size2D();
	
	// 有加速度通常代表玩家仍在给移动输入。
	bHasAcceleration = OwningMovementComponent->GetCurrentAcceleration().SizeSquared2D() > 0.f;
	
	// 加一个很小阈值，避免速度接近 0 时因为浮点误差在 Idle/Move 之间抖动。  AI提议的
	bShouldMove = GroundSpeed > 3.f && bHasAcceleration;
}
