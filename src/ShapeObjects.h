#pragma once

#include <Arduino.h>

typedef enum 
{
    triangle,
    unknown
} ShapeKind;

typedef struct Shape
{
    //kind of shape
    ShapeKind kind;
    //number of child based on the kind
    // * triangle : 2 connections
    struct Shape **connections;
    void * content;
} Shape;

String shapeKindToString(const ShapeKind kind);
ShapeKind stringToShapeKind(const String & kind);