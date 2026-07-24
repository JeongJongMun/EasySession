// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EasyExampleCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;

/**
 * Minimal third person character for the example content. Games are expected to
 * replace it with their own character - it only exists so the example maps are
 * playable out of the box in any project.
 *
 * Input is bound directly to keys (WASD + mouse + space) in
 * SetupPlayerInputComponent, so it works without any input mapping assets or
 * project input settings. Networked movement comes from ACharacter.
 */
UCLASS(Blueprintable, NotPlaceable)
class AEasyExampleCharacter : public ACharacter
{
	GENERATED_BODY()

public:

	AEasyExampleCharacter();

	//~ Begin APawn Interface
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	//~ End APawn Interface

private:

	/** Move along the control rotation's yaw. Axis-key handlers receive 1 while held. */
	void MoveForward(float Value);
	void MoveBackward(float Value);
	void MoveRight(float Value);
	void MoveLeft(float Value);

	/** Mouse look. */
	void LookYaw(float Value);
	void LookPitch(float Value);

	/** Add movement along a horizontal axis of the control rotation. */
	void AddControlSpaceMovement(EAxis::Type Axis, float Value);

	/** Third person camera boom. */
	UPROPERTY(VisibleAnywhere, Category = "EasySession")
	TObjectPtr<USpringArmComponent> SpringArm;

	/** Third person camera. */
	UPROPERTY(VisibleAnywhere, Category = "EasySession")
	TObjectPtr<UCameraComponent> Camera;
};
