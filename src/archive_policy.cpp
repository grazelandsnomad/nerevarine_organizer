#include "archive_policy.h"

namespace archive_policy {

Action onOutcome(Outcome outcome)
{
    switch (outcome) {
    case Outcome::Cancelled:
    case Outcome::NothingPicked:
    case Outcome::ExtractorMissing:
        return Action::Keep;
    case Outcome::Installed:
    case Outcome::VerifyFailed:
    case Outcome::ExtractFailed:
    case Outcome::RowGone:
        return Action::Delete;
    }
    return Action::Delete;
}

} // namespace archive_policy
