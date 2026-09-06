// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstances/FirstDKAnimInstance.h"

#include "Character/BaseCharacter.h"
#include "Character/DKCharacter.h"
#include "Components/Targeting/DKTargetLockComponent.h"
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

	const FVector WorldVelocity = OwningCharacter->GetVelocity();
	const FVector HorizontalVelocity(
		WorldVelocity.X,
		WorldVelocity.Y,
		0.f);

	GroundSpeed = HorizontalVelocity.Size();

	if (GroundSpeed > 3.f)
	{
		// 世界速度转换成相对角色朝向的本地速度：X=前后，Y=左右。
		const FVector LocalVelocity =
			OwningCharacter->GetActorTransform()
				.InverseTransformVectorNoScale(HorizontalVelocity);

		const FVector2D LocalDirection =
			FVector2D(LocalVelocity.X, LocalVelocity.Y).GetSafeNormal();

		LocalForwardDirection = LocalDirection.X;
		LocalRightDirection = LocalDirection.Y;
	}
	else
	{
		LocalForwardDirection = 0.f;
		LocalRightDirection = 0.f;
	}

	WalkAnimationPlayRate = GroundSpeed > 3.f
		? FMath::Clamp(
			GroundSpeed / FMath::Max(WalkReferenceSpeed, 1.f),
			0.5f,
			1.25f)
		: 1.f;

	bHasAcceleration =
		OwningMovementComponent->GetCurrentAcceleration().SizeSquared2D() > 0.f;

	// 保持现有规则，避免本册改变主角原来的状态机行为。
	bShouldMove = GroundSpeed > 3.f && bHasAcceleration;

	// 以下是项目现有的主角锁定移动逻辑，必须继续保留。
	// 它使用玩家输入方向；上面的 LocalForward/Right 使用实际速度，二者用途不同。
	MoveForward = 0.f;
	MoveRight = 0.f;
	bIsTargetLocked = false;
	bUseDirectionalLocomotion = false;

	const ADKCharacter* DKCharacter = Cast<ADKCharacter>(OwningCharacter);
	if (DKCharacter)
	{
		bIsTargetLocked =
			DKCharacter->GetTargetLockComponent() &&
			DKCharacter->GetTargetLockComponent()->IsTargetLocked();

		// 锁定和格挡都会关闭“朝移动方向旋转”并改用控制器期望朝向。
		// AnimBP 以此选择八向移动，但真实的锁定状态仍由 bIsTargetLocked 表示。
		bUseDirectionalLocomotion =
			bIsTargetLocked ||
			(!OwningMovementComponent->bOrientRotationToMovement &&
				OwningMovementComponent->bUseControllerDesiredRotation);

		if (bUseDirectionalLocomotion)
		{
			const FVector InputVector =
				OwningCharacter->GetLastMovementInputVector();

			if (!InputVector.IsNearlyZero())
			{
				const FVector InputFlat =	FVector(InputVector.X, InputVector.Y, 0.f).GetSafeNormal();

				const FVector Forward =	OwningCharacter->GetActorForwardVector();
				const FVector Right =	OwningCharacter->GetActorRightVector();

				MoveForward = FMath::Clamp(	FVector::DotProduct(InputFlat, Forward),-1.f,	1.f);

				MoveRight = FMath::Clamp(	FVector::DotProduct(InputFlat, Right),	-1.f,	1.f);
			}
		}
	}
}
