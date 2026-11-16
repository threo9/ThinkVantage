#pragma once
#include <QString>
namespace RefreshSafety {
// Only the exact display/mode installed by this transaction may be reverted.
// A newer external mode or a recycled display source owns its current state.
struct Mode {
    QString identity;
    int width=0,height=0,bits=0,hz=0,orientation=0,flags=0;
    long x=0,y=0;
    bool operator==(const Mode &) const = default;
};
enum class Decision { Keep, Restore, Superseded, InvalidToken };
inline Decision decide(const QString &ownedToken,const QString &providedToken,const Mode &expected,const Mode &current,bool keep){
    if(ownedToken.isEmpty()||providedToken!=ownedToken)return Decision::InvalidToken;
    if(expected.identity.isEmpty()||!(expected==current))return Decision::Superseded;
    return keep?Decision::Keep:Decision::Restore;
}
}
