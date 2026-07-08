// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/HeroCharacter.h"

#include "EnhancedInputSubsystems.h"
#include "MyGameplayTags.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/Input/FirstInputComponent.h"
#include "DataAssets/Input/DataAsset_InputConfig.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

AHeroCharacter::AHeroCharacter()
{
	// CapsuleComponent 继承自 ACharacter，是角色碰撞和根组件。
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	// 镜头转动不直接带动角色旋转；角色朝向由移动方向决定。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 创建相机摇臂并挂到角色根组件（胶囊体）上。
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->TargetArmLength = 350.f;
	CameraBoom->SocketOffset = FVector(0.f, 50.f, 60.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;

	// 相机挂到摇臂末端。旋转由 CameraBoom 继承 Controller 控制。
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// CharacterMovement 已由 ACharacter 创建，这里只获取并配置，
	// 不要再次 CreateDefaultSubobject 一个移动组件。
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 600.f, 0.f);
	Movement->MaxWalkSpeed = 500.f;
	Movement->BrakingDecelerationWalking = 1800.f;
	Movement->JumpZVelocity = 600.f;
	Movement->AirControl = 0.35f;
	Movement->GravityScale = 1.7f;

	JumpMaxCount = 1;
}

void AHeroCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	//// 保留父类输入初始化。即使当前 BaseCharacter 没有绑定，也不应省略。
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	
	checkf(InputConfigDataAsset,
		TEXT("[%s] 没有配置 InputConfigDataAsset"), *GetNameSafe(this));
	
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	check(PlayerController);
	
	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	check(LocalPlayer);
	
	// Mapping Context 属于本地玩家，而不是角色 Actor 本身。
	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);
	check(Subsystem);
	
	// 优先级 0 是基础输入环境；菜单、锁定或载具可使用更高优先级覆盖。
	Subsystem->AddMappingContext(InputConfigDataAsset->DefaultMappingContext,0);
	
	// 此 Cast 要求 Project Settings 中已将默认输入组件设为 FirstInputComponent。
	UFirstInputComponent* FirstInput = CastChecked<UFirstInputComponent>(PlayerInputComponent);
	
	FirstInput->BindNativeInputAction(
	   InputConfigDataAsset,
	   MyGameplayTags::Input_Native_Move,
	   ETriggerEvent::Triggered,
	   this,
	   &ThisClass::Input_Move);

	FirstInput->BindNativeInputAction(
		InputConfigDataAsset,
		MyGameplayTags::Input_Native_Look,
		ETriggerEvent::Triggered,
		this,
		&ThisClass::Input_Look);

	FirstInput->BindNativeInputAction(
		InputConfigDataAsset,
		MyGameplayTags::Input_Native_Jump,
		ETriggerEvent::Started,
		this,
		&ThisClass::Input_JumpStarted);

	FirstInput->BindNativeInputAction(
		InputConfigDataAsset,
		MyGameplayTags::Input_Native_Jump,
		ETriggerEvent::Completed,
		this,
		&ThisClass::Input_JumpCompleted);

	FirstInput->BindNativeInputAction(
	InputConfigDataAsset,
	MyGameplayTags::Input_Native_Jump,
	ETriggerEvent::Canceled,
	this,
	&ThisClass::Input_JumpCompleted);
	
	FirstInput->BindAbilityInputActions(
		InputConfigDataAsset,
		this,
		&ThisClass::Input_AbilityInputPressed,
		&ThisClass::Input_AbilityInputReleased);
}

void AHeroCharacter::Input_Move(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}
	
	const FVector2D MovementVector = Value.Get<FVector2D>();
	// 只使用控制器 Yaw，避免镜头向上看时角色向天空移动。
	const FRotator MovementRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	
	const FVector ForwardDirection = MovementRotation.RotateVector(FVector::ForwardVector);
	const FVector RightDirection = MovementRotation.RotateVector(FVector::RightVector);
	
	// Enhanced Input 的二维值约定：Y 前后，X 左右。
	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
	
}

void AHeroCharacter::Input_Look(const FInputActionValue& Value)
{
	const FVector2D LookVector = Value.Get<FVector2D>();
	AddControllerYawInput(LookVector.X);
	AddControllerPitchInput(LookVector.Y);
	
}

void AHeroCharacter::Input_JumpStarted(const FInputActionValue& Value)
{
	// 起跳物理由 CharacterMovement 负责。
	Jump();
}

void AHeroCharacter::Input_JumpCompleted(const FInputActionValue& Value)
{
	// 松开按键后停止继续施加跳跃保持时间，支持可变跳跃高度
	StopJumping();
}

void AHeroCharacter::Input_AbilityInputPressed(FGameplayTag InputTag)
{
}

void AHeroCharacter::Input_AbilityInputReleased(FGameplayTag InputTag)
{
}
