#include "platform/MacAXElement.h"

namespace snaptray::platform::mac {

bool getAXElementFrame(AXUIElementRef element, CGRect& outFrame)
{
    if (!element) {
        return false;
    }

    CFTypeRef posRef = nullptr;
    CFTypeRef sizeRef = nullptr;

    AXError err = AXUIElementCopyAttributeValue(element, kAXPositionAttribute, &posRef);
    if (err != kAXErrorSuccess || !posRef) return false;

    if (CFGetTypeID(posRef) != AXValueGetTypeID()) {
        CFRelease(posRef);
        return false;
    }

    err = AXUIElementCopyAttributeValue(element, kAXSizeAttribute, &sizeRef);
    if (err != kAXErrorSuccess || !sizeRef) {
        CFRelease(posRef);
        return false;
    }

    if (CFGetTypeID(sizeRef) != AXValueGetTypeID()) {
        CFRelease(posRef);
        CFRelease(sizeRef);
        return false;
    }

    CGPoint origin;
    CGSize size;
    Boolean gotPos = AXValueGetValue((AXValueRef)posRef, (AXValueType)kAXValueCGPointType, &origin);
    Boolean gotSize = AXValueGetValue((AXValueRef)sizeRef, (AXValueType)kAXValueCGSizeType, &size);

    CFRelease(posRef);
    CFRelease(sizeRef);

    if (!gotPos || !gotSize) return false;

    outFrame = CGRectMake(origin.x, origin.y, size.width, size.height);
    return true;
}

QString getAXElementRole(AXUIElementRef element)
{
    if (!element) {
        return QString();
    }

    CFTypeRef roleRef = nullptr;
    AXError err = AXUIElementCopyAttributeValue(element, kAXRoleAttribute, &roleRef);
    if (err != kAXErrorSuccess || !roleRef) return QString();

    if (CFGetTypeID(roleRef) != CFStringGetTypeID()) {
        CFRelease(roleRef);
        return QString();
    }

    QString role = QString::fromCFString((CFStringRef)roleRef);
    CFRelease(roleRef);
    return role;
}

std::optional<bool> copyAXBooleanAttribute(AXUIElementRef element, CFStringRef attribute)
{
    if (!element || !attribute) {
        return std::nullopt;
    }

    CFTypeRef valueRef = nullptr;
    AXError err = AXUIElementCopyAttributeValue(element, attribute, &valueRef);
    if (err != kAXErrorSuccess || !valueRef) {
        return std::nullopt;
    }

    std::optional<bool> result;
    if (CFGetTypeID(valueRef) == CFBooleanGetTypeID()) {
        result = CFBooleanGetValue(static_cast<CFBooleanRef>(valueRef));
    } else if (CFGetTypeID(valueRef) == CFNumberGetTypeID()) {
        int intValue = 0;
        if (CFNumberGetValue(static_cast<CFNumberRef>(valueRef), kCFNumberIntType, &intValue)) {
            result = intValue != 0;
        }
    }

    CFRelease(valueRef);
    return result;
}

std::optional<double> copyAXNumberAttribute(AXUIElementRef element, CFStringRef attribute)
{
    if (!element || !attribute) {
        return std::nullopt;
    }

    CFTypeRef valueRef = nullptr;
    const AXError err = AXUIElementCopyAttributeValue(element, attribute, &valueRef);
    if (err != kAXErrorSuccess || !valueRef) {
        return std::nullopt;
    }

    std::optional<double> result;
    double value = 0.0;
    if (CFGetTypeID(valueRef) == CFNumberGetTypeID() &&
        CFNumberGetValue(static_cast<CFNumberRef>(valueRef), kCFNumberDoubleType, &value)) {
        result = value;
    }
    CFRelease(valueRef);
    return result;
}

AXUIElementRef copyAXElementAttribute(AXUIElementRef element, CFStringRef attribute)
{
    if (!element || !attribute) {
        return nullptr;
    }

    CFTypeRef valueRef = nullptr;
    const AXError err = AXUIElementCopyAttributeValue(element, attribute, &valueRef);
    if (err != kAXErrorSuccess || !valueRef) {
        return nullptr;
    }
    if (CFGetTypeID(valueRef) != AXUIElementGetTypeID()) {
        CFRelease(valueRef);
        return nullptr;
    }
    return static_cast<AXUIElementRef>(valueRef);
}

} // namespace snaptray::platform::mac
