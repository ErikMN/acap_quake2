/**
 * This file contains the theme configuration for the app.
 */
import { createTheme } from '@mui/material/styles';
import { grey } from '@mui/material/colors';

const lightTheme = createTheme({
  palette: {
    mode: 'light',
    primary: {
      main: '#8a5a2b',
      contrastText: '#ffffff'
    },
    secondary: {
      main: '#4f565e'
    },
    background: {
      default: '#ece9e2',
      paper: '#ffffff'
    },
    text: {
      primary: '#1c1f22',
      secondary: '#555b61'
    },
    error: {
      main: '#d32f2f'
    },
    warning: {
      main: '#b66a1f'
    },
    info: {
      main: '#2979a8'
    },
    success: {
      main: '#4f8a5b'
    }
  },
  typography: {
    fontFamily: 'Segoe UI, Roboto, Arial, sans-serif',
    h1: {
      fontSize: '2.5rem',
      fontWeight: 700
    },
    h5: {
      fontSize: '22px'
    },
    h6: {
      fontSize: '16px',
      fontWeight: 500
    },
    body1: {
      fontSize: '0.875rem',
      lineHeight: 1.5
    },
    button: {
      textTransform: 'none'
    }
  },
  shape: {
    borderRadius: 8
  },
  components: {
    MuiCssBaseline: {
      styleOverrides: (theme) => ({
        body: {
          scrollbarColor: `${theme.palette.grey[500]} ${theme.palette.background.default}`,
          scrollbarWidth: 'thin'
        },
        '::-webkit-scrollbar': {
          width: '8px',
          height: '8px'
        },
        '::-webkit-scrollbar-thumb': {
          backgroundColor: theme.palette.grey[400],
          borderRadius: '4px'
        },
        '::-webkit-scrollbar-track': {
          backgroundColor: theme.palette.background.default
        }
      })
    },
    MuiButton: {
      styleOverrides: {
        root: {
          borderRadius: 8
        },
        contained: {
          boxShadow: '0px 3px 6px rgba(0, 0, 0, 0.2)'
        }
      }
    },
    MuiAppBar: {
      styleOverrides: {
        colorPrimary: {
          backgroundColor: '#ffffff'
        }
      }
    },
    MuiCard: {
      styleOverrides: {
        root: {
          boxShadow: '0px 3px 10px rgba(0, 0, 0, 0.1)',
          borderRadius: 8
        }
      }
    }
  }
});

const darkTheme = createTheme({
  palette: {
    mode: 'dark',
    primary: {
      main: '#d18b47',
      contrastText: '#111111'
    },
    secondary: {
      main: '#292929'
    },
    background: {
      default: '#151515',
      paper: '#1d1f21'
    },
    text: {
      primary: '#ffffff',
      secondary: '#d6d6d6'
    },
    error: {
      main: '#f44336'
    },
    warning: {
      main: '#d18b47'
    },
    info: {
      main: '#29b6f6'
    },
    success: {
      main: '#66bb6a'
    }
  },
  typography: {
    fontFamily: 'Segoe UI, Roboto, Arial, sans-serif',
    h1: {
      fontSize: '2.5rem',
      fontWeight: 700
    },
    h5: {
      color: '#d6d6d6',
      fontSize: '22px'
    },
    h6: {
      color: '#d6d6d6',
      fontSize: '16px',
      fontWeight: 500
    },
    body1: {
      fontSize: '0.875rem',
      lineHeight: 1.5
    },
    button: {
      textTransform: 'none'
    }
  },
  shape: {
    borderRadius: 8
  },
  components: {
    MuiCssBaseline: {
      styleOverrides: (theme) => ({
        body: {
          scrollbarColor: `${theme.palette.grey[500]} ${theme.palette.background.default}`,
          scrollbarWidth: 'thin'
        },
        '::-webkit-scrollbar': {
          width: '8px',
          height: '8px'
        },
        '::-webkit-scrollbar-thumb': {
          backgroundColor: theme.palette.grey[700],
          borderRadius: '4px'
        },
        '::-webkit-scrollbar-track': {
          backgroundColor: theme.palette.background.default
        }
      })
    },
    MuiButton: {
      styleOverrides: {
        root: {
          borderRadius: 8
        },
        contained: {
          boxShadow: '0px 3px 6px rgba(0, 0, 0, 0.2)'
        }
      }
    },
    MuiAppBar: {
      styleOverrides: {
        colorPrimary: {
          backgroundColor: grey[800]
        }
      }
    },
    MuiCard: {
      styleOverrides: {
        root: {
          boxShadow: '0px 3px 10px rgba(0, 0, 0, 0.1)',
          borderRadius: 8
        }
      }
    }
  }
});

export { lightTheme, darkTheme };
