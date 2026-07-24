// Copyright Langerak. All Rights Reserved.

#include "EasyExampleCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"

AEasyExampleCharacter::AEasyExampleCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// The camera follows the control rotation; the character turns toward where it moves.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Mesh transform for a standard humanoid - the Blueprint child only picks the asset.
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -88.0f), FRotator(0.0f, -90.0f, 0.0f));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 400.0f;
	SpringArm->bUsePawnControlRotation = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
}

void AEasyExampleCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Keys are bound directly so the example needs no input assets or project
	// settings. Axis-key bindings deliver 1 every frame while the key is held.
	PlayerInputComponent->BindAxisKey(EKeys::W, this, &AEasyExampleCharacter::MoveForward);
	PlayerInputComponent->BindAxisKey(EKeys::S, this, &AEasyExampleCharacter::MoveBackward);
	PlayerInputComponent->BindAxisKey(EKeys::D, this, &AEasyExampleCharacter::MoveRight);
	PlayerInputComponent->BindAxisKey(EKeys::A, this, &AEasyExampleCharacter::MoveLeft);
	PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &AEasyExampleCharacter::LookYaw);
	PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &AEasyExampleCharacter::LookPitch);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &ACharacter::StopJumping);
}

void AEasyExampleCharacter::MoveForward(float Value)
{
	AddControlSpaceMovement(EAxis::X, Value);
}

void AEasyExampleCharacter::MoveBackward(float Value)
{
	AddControlSpaceMovement(EAxis::X, -Value);
}

void AEasyExampleCharacter::MoveRight(float Value)
{
	AddControlSpaceMovement(EAxis::Y, Value);
}

void AEasyExampleCharacter::MoveLeft(float Value)
{
	AddControlSpaceMovement(EAxis::Y, -Value);
}

void AEasyExampleCharacter::LookYaw(float Value)
{
	AddControllerYawInput(Value);
}

void AEasyExampleCharacter::LookPitch(float Value)
{
	AddControllerPitchInput(Value);
}

void AEasyExampleCharacter::AddControlSpaceMovement(EAxis::Type Axis, float Value)
{
	if (Controller == nullptr || Value == 0.0f)
	{
		return;
	}

	const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(Axis), Value);
}
