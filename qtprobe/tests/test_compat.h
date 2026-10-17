#pragma once
#include <QtTest>

// Qt 5 predates the replacement for Qt 6's deprecated exception assertion.
#ifndef QVERIFY_THROWS_EXCEPTION
#define QVERIFY_THROWS_EXCEPTION(exception, expression) QVERIFY_EXCEPTION_THROWN(expression, exception)
#endif
