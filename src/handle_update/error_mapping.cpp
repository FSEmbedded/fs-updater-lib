#include "error_mapping.h"

#include "fs_exceptions.h"

namespace fs {

ErrorInfo classify_active_exception() noexcept
{
    // Precondition: an exception is in flight (called from a catch block).
    // The re-raise drives dispatch on the dynamic type without RTTI; the order
    // is most-derived first, mirroring the CLI's catch ladder.
    try {
        throw;
    } catch (const UpdateInProgress&) {
        return ErrorInfo{Error::update_in_progress, 0};
    } catch (const GenericException& g) {
        return ErrorInfo{Error::generic, g.errorno};
    } catch (const NotAllowedUpdateState&) {
        return ErrorInfo{Error::not_allowed_state, 0};
    } catch (const BaseFSUpdateException&) {
        return ErrorInfo{Error::internal, 0};
    } catch (...) {
        return ErrorInfo{Error::system, 0};
    }
}

} // namespace fs
