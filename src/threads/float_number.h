#ifndef __THREAD_FLOAT_NUMBER_H
#define __THREAD_FLOAT_NUMBER_H

 /*16 LSB used for fractional part*/
#define FP_SHIFT_AMOUNT 16
/*value -> floated value*/
#define FP_CONVERT(A) ((int)(A << FP_SHIFT_AMOUNT))
/* DIRECTLY USE: add two float numbers*/
/*add float A to int B*/
#define FP_MIX_ADD(A,B) (A + (B << FP_SHIFT_AMOUNT))
/* DIRECTLY USE: floatA - floatB*/
/*float A - int B*/
#define FP_MIX_SUB(A,B) (A - (B << FP_SHIFT_AMOUNT))
/*floatA * floatB = ((int64_t) x) * y / f ,f = 1<<16*/
#define FP_MULTIPLY(A,B) ((int)(((int64_t) A) * B >> FP_SHIFT_AMOUNT))
/* DIRECTLY USE: floatA * intB */
/*floatA / floatB = ((int64_t) x) * f / y */
#define FP_DIVIDE(A,B) ((int)((((int64_t) A) << FP_SHIFT_AMOUNT) / B))
/* DIRECTLY USE: floatA / intB */
/* get the integer part of a float*/
#define GET_INT_PART(A) (A >> FP_SHIFT_AMOUNT)
/*floatA -> rounded integer*/
#define FP_ROUND(A) (A >= 0 ? ((A + (1 << (FP_SHIFT_AMOUNT - 1))) >> FP_SHIFT_AMOUNT) : ((A - (1 << (FP_SHIFT_AMOUNT - 1))) >> FP_SHIFT_AMOUNT)) 
#endif