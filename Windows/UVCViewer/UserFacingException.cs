using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace UVCViewer {
    class UserFacingException : Exception {
        public UserFacingException(string message) : base(message) {
        }
    }
}
