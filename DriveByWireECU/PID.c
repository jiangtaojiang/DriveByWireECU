/**
 * @author Nick Mosher, <codewhisperer97@gmail.com>
 *
 * A PID Controller is a method of system control in which a correctional output
 * is generated to guide the system toward a desired setpoint (aka target).
 * The PID Controller calculates the output based on the following factors:
 *
 *		Gains (proportional, integral, and derivative)
 *		Target
 *		Feedback
 *
 * The gain values act as multipliers for their corresponding components of PID
 * (more detail later).	The target is the value which the system strives to
 * reach by manipulating the output.	The feedback is the system's actual
 * position or status in regards to the physical world.
 * Another important term in PID is "error", which refers to the difference
 * between the target and the feedback.
 *
 * Each of the three components of PID contributes a unique behavior to the
 * system.
 *
 *		The Proportional component introduces a linear relationship between the
 *		error (target minus feedback) and the output.	This means that as the
 *		feedback grows further away from the target, the output grows
 *		proportionally stronger.
 *
 *				Proportional component = (P Gain) * (target - feedback)
 *
 *		The Integral component is designed to give a very precise approach of the
 *		feedback to the target.	Depending on the scale of the physical system
 *		and the precision of feedback (e.g. sensors), the proportional component
 *		alone is likely not sufficient to provide adequate power (e.g. to motors)
 *		to guide the system in regards to small-scale corrections.	The Integral
 *		component integrates the error of the system (target - feedback) over
 *		time.	If the system reaches a point where it is close but not exactly
 *		on top of the target, the integration will slowly build until it is
 *		powerful enough to overcome static resistances and move the system
 *		preciesly to the target.
 *
 *				Integral component = (I Gain) * Integral of error over time
 *
 *					*In this implementation, Integral is calculated with a running
 *					summation of the system's error, updated at each tick.
 *
 *		The Derivative component measures the rate of change of the feedback.
 *		It can reduce the strength of the output if the feedback is approaching
 *		the target too quickly or if the feedback is moving away from the target.
 *
 *				Derivative component = (D Gain) * ((error - lastError) / time - lastTime)
 *
 * The output generated by the PID Controller is the sum of the three
 * components.
 *
 *		PID output = Proportional component + Integral component + Derivative component
 */

#include <stdlib.h>
#include <stdint.h>
#include "PID.h"

/**
 * Constructs the PIDController object with PID Gains and function pointers
 * for retrieving feedback (pidSource) and delivering output (pidOutput).
 * All PID gains should be positive, otherwise the system will violently diverge
 * from the target.
 * @param p The Proportional gain.
 * @param i The Integral gain.
 * @param d The Derivative gain.
 * @param (*pidSource) The function pointer for retrieving system feedback.
 * @param (*pidOutput) The function pointer for delivering system output.
 */
PIDController *createPIDController(double p, double i, double d, int (*pidSource)(void), void (*pidOutput)(int output)) {

	PIDController *controller = malloc(sizeof(PIDController));
	controller->p = p;
	controller->i = i;
	controller->d = d;
	controller->target = 0;
	controller->output = 0;
	controller->enabled = 1;
	controller->currentFeedback = 0;
	controller->lastFeedback = 0;
	controller->lastError = 0;
	controller->currentTime = 0L;
	controller->lastTime = 0L;
	controller->integralCumulation = 0;
	controller->maxCumulation = 30000;
	controller->inputBounded = 0;
	controller->outputBounded = 0;
	controller->inputLowerBound = 0;
	controller->inputUpperBound = 0;
	controller->outputBounded = 0;
	controller->outputLowerBound = 0;
	controller->outputUpperBound = 0;
	controller->timeFunctionRegistered = 0;
	controller->pidSource = pidSource;
	controller->pidOutput = pidOutput;
	controller->getSystemTime = NULL;
	return controller;
}

/**
 * This method uses the established function pointers to retrieve system
 * feedback, calculate the PID output, and deliver the correction value
 * to the parent of this PIDController.	This method should be run as
 * fast as the source of the feedback in order to provide the highest
 * resolution of control (for example, to be placed in the loop() method).
 */
void tick(PIDController *c) {

	if(c->enabled) {
		//Retrieve system feedback from user callback.
		c->currentFeedback = c->pidSource();

		//Apply input bounds if necessary.
		if(c->inputBounded) {
			if(c->currentFeedback > c->inputUpperBound) c->currentFeedback = c->inputUpperBound;
			if(c->currentFeedback < c->inputLowerBound) c->currentFeedback = c->inputLowerBound;
		}

		/*
		 * Feedback wrapping causes two distant numbers to appear adjacent to one
		 * another for the purpose of calculating the system's error.
		 */
		if(c->feedbackWrapped) {

			/*
			 * There are three ways to traverse from one point to another in this setup.
			 *
			 *		1)	Target --> Feedback
			 *
			 * The other two ways involve bridging a gap connected by the upper and
			 * lower bounds of the feedback wrap.
			 *
			 *		2)	Target --> Upper Bound == Lower Bound --> Feedback
			 *
			 *		3)	Target --> Lower Bound == Upper Bound --> Feedback
			 *
			 * Of these three paths, one should always be shorter than the other two,
			 * unless all three are equal, in which case it does not matter which path
			 * is taken.
			 */
			int regErr = c->target - c->currentFeedback;
			int altErr1 = (c->target - c->feedbackWrapLowerBound) + (c->feedbackWrapUpperBound - c->currentFeedback);
			int altErr2 = (c->feedbackWrapUpperBound - c->target) + (c->currentFeedback - c->feedbackWrapLowerBound);

			// Calculate the absolute values of each error.
			int regErrAbs = (regErr >= 0) ? regErr : -regErr;
			int altErr1Abs = (altErr1 >= 0) ? altErr1 : -altErr1;
			int altErr2Abs = (altErr2 >= 0) ? altErr2 : -altErr2;

			// Use the error with the smallest absolute value
			if(regErrAbs <= altErr1Abs && regErr <= altErr2Abs) {
				c->error = regErr;
			}
			else if(altErr1Abs < regErrAbs && altErr1Abs < altErr2Abs) {
				c->error = altErr1Abs;
			}
			else if(altErr2Abs < regErrAbs && altErr2Abs < altErr1Abs) {
				c->error = altErr2Abs;
			}
		}
		else {
			// Calculate the error between the feedback and the target.
			c->error = c->target - c->currentFeedback;
		}

		// If we have a registered way to retrieve the system time, use time in PID calculations.
		if(c->timeFunctionRegistered) {
			// Retrieve system time
			c->currentTime = c->getSystemTime();

			// Calculate time since last tick() cycle.
			long deltaTime = c->currentTime - c->lastTime;

			// Calculate the integral of the feedback data since last cycle.
			int cycleIntegral = (c->lastError + c->error / 2) * deltaTime;

			// Add this cycle's integral to the integral cumulation.
			c->integralCumulation += cycleIntegral;

			// Calculate the slope of the line with data from the current and last cycles.
			c->cycleDerivative = (c->error - c->lastError) / deltaTime;

			// Save time data for next iteration.
			c->lastTime = c->currentTime;
		}
		// If we have no way to retrieve system time, estimate calculations.
		else {
			c->integralCumulation += c->error;
			c->cycleDerivative = (c->error - c->lastError);
		}

		// Prevent the integral cumulation from becoming overwhelmingly huge.
		if(c->integralCumulation > c->maxCumulation) c->integralCumulation = c->maxCumulation;
		if(c->integralCumulation < -c->maxCumulation) c->integralCumulation = -c->maxCumulation;

		// Calculate the system output based on data and PID gains.
		c->output = (int) ((c->error * c->p) + (c->integralCumulation * c->i) + (c->cycleDerivative * c->d));

		// Save a record of this iteration's data.
		c->lastFeedback = c->currentFeedback;
		c->lastError = c->error;

		// Trim the output to the bounds if needed.
		if(c->outputBounded) {
			if(c->output > c->outputUpperBound) c->output = c->outputUpperBound;
			if(c->output < c->outputLowerBound) c->output = c->outputLowerBound;
		}

		c->pidOutput(c->output);
	}
}

/**
 * Enables or disables this PIDController.
 * @param True to enable, False to disable.
 */
void setEnabled(PIDController *controller, uint8_t enabled) {

	// If the PIDController was enabled and is being disabled.
	if(!enabled && controller->enabled) {
		controller->output = 0;
		controller->integralCumulation = 0;
	}
	controller->enabled = enabled;
}

/**
 * Returns the value that the Proportional component is contributing to the output.
 * @return The value that the Proportional component is contributing to the output.
 */
int getProportionalComponent(PIDController *controller) {
	return (controller->error * controller->p);
}

/**
 * Returns the value that the Integral component is contributing to the output.
 * @return The value that the Integral component is contributing to the output.
 */
int getIntegralComponent(PIDController *controller) {
	return (controller->integralCumulation * controller->i);
}

/**
 * Returns the value that the Derivative component is contributing to the output.
 * @return The value that the Derivative component is contributing to the output.
 */
int getDerivativeComponent(PIDController *controller) {
	return (controller->cycleDerivative * controller->d);
}

/**
 * Sets the maximum value that the integral cumulation can reach.
 * @param max The maximum value of the integral cumulation.
 */
void setMaxIntegralCumulation(PIDController *controller, int max) {

	// If the new max value is less than 0, invert to make positive.
	if(max < 0) {
		max = -max;
	}

	// If the new max is not more than 1 then the cumulation is useless.
	if(max > 1) {
		controller->maxCumulation = max;
	}
}

/**
 * Sets bounds which limit the lower and upper extremes that this PIDController
 * accepts as inputs.	Outliers are trimmed to the lower and upper bounds.
 * Setting input bounds automatically enables input bounds.
 * @param lower The lower input bound.
 * @param upper The upper input bound.
 */
void setInputBounds(PIDController *controller, int lower, int upper) {

	if(upper > lower) {
		controller->inputBounded = 1;
		controller->inputUpperBound = upper;
		controller->inputLowerBound = lower;
	}
}

/**
 * Sets bounds which limit the lower and upper extremes that this PIDController
 * will ever generate as output. Setting output bounds automatically enables
 * output bounds.
 * @param lower The lower output bound.
 * @param upper The upper output bound.
 */
void setOutputBounds(PIDController *controller, int lower, int upper) {

	if(upper > lower) {
		controller->outputBounded = 1;
		controller->outputLowerBound = lower;
		controller->outputUpperBound = upper;
	}
}

/**
 * Sets the bounds which the feedback wraps around. This
 * also enables Input Bounds at the same coordinates to
 * prevent extraneous domain errors.
 * @param lower The lower wrap bound.
 * @param upper The upper wrap bound.
 */
void setFeedbackWrapBounds(PIDController *controller, int lower, int upper) {

	// Make sure no value outside this circular range is ever input.
	setInputBounds(controller, lower, upper);

	controller->feedbackWrapped = 1;
	controller->feedbackWrapLowerBound = lower;
	controller->feedbackWrapUpperBound = upper;
}
